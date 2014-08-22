#include <bts/blockchain/withdraw_types.hpp>
#include <bts/cli/cli.hpp>
#include <bts/cli/pretty.hpp>
#include <bts/rpc/exceptions.hpp>
#include <bts/wallet/pretty.hpp>
#include <bts/wallet/wallet.hpp>
#include <bts/wallet/url.hpp>

#include <fc/io/buffered_iostream.hpp>
#include <fc/io/console.hpp>
#include <fc/io/json.hpp>
#include <fc/io/sstream.hpp>
#include <fc/log/logger.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/thread/thread.hpp>
#include <fc/variant.hpp>

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/range/algorithm/max_element.hpp>
#include <boost/range/algorithm/min_element.hpp>

#include <iomanip>
#include <iostream>

#ifdef HAVE_READLINE
# include <readline/readline.h>
# include <readline/history.h>
// I don't know exactly what version of readline we need.  I know the 4.2 version that ships on some macs is
// missing some functions we require.  We're developing against 6.3, but probably anything in the 6.x 
// series is fine
# if RL_VERSION_MAJOR < 6
#  ifdef _MSC_VER
#   pragma message("You have an old version of readline installed that might not support some of the features we need")
#   pragma message("Readline support will not be compiled in")
#  else
#   warning "You have an old version of readline installed that might not support some of the features we need"
#   warning "Readline support will not be compiled in"
#  endif
#  undef HAVE_READLINE
# endif
#endif

namespace bts { namespace cli {
  
  namespace detail
  {
      class cli_impl
      {
         public:
            bts::client::client*                            _client;
            rpc_server_ptr                                  _rpc_server;
            bts::cli::cli*                                  _self;
            fc::thread                                      _cin_thread;
                                                            
            bool                                            _quit;
            bool                                            show_raw_output;
            bool                                            _daemon_mode;

            boost::iostreams::stream< boost::iostreams::null_sink > nullstream;
            
            std::ostream*                  _saved_out;
            std::ostream*                  _out;   //cout | log_stream | tee(cout,log_stream) | null_stream
            std::istream*                  _command_script;
            std::istream*                  _input_stream;
            boost::optional<std::ostream&> _input_stream_log;


            cli_impl(bts::client::client* client, std::istream* command_script, std::ostream* output_stream);

            void process_commands(std::istream* input_stream);

            void start()
            {
                try
                {
                  if (_command_script)
                    process_commands(_command_script);
                  if (_daemon_mode)
                  {
                    _rpc_server->wait_till_rpc_server_shutdown();
                    return;
                  }
                  else if (!_quit)
                    process_commands(&std::cin);
                  if( _rpc_server )
                     _rpc_server->shutdown_rpc_server();
                }
                catch ( const fc::exception& e)
                {
                    *_out << "\nshutting down\n";
                    elog( "${e}", ("e",e.to_detail_string() ) );
                    _rpc_server->shutdown_rpc_server();
                }
            }

            string get_prompt()const
            {
              string wallet_name =  _client->get_wallet()->get_wallet_name();
              string prompt = wallet_name;
              if( prompt == "" )
              {
                 prompt = "(wallet closed) " CLI_PROMPT_SUFFIX;
              }
              else
              {
                 if( _client->get_wallet()->is_locked() )
                    prompt += " (locked) " CLI_PROMPT_SUFFIX;
                 else
                    prompt += " (unlocked) " CLI_PROMPT_SUFFIX;
              }
              return prompt;
            }

            void parse_and_execute_interactive_command(string command, 
                                                       fc::istream_ptr argument_stream )
            { 
              if( command.size() == 0 )
                 return;
              if (command == "enable_raw")
              {
                  show_raw_output = true;
                  return;
              }
              else if (command == "disable_raw")
              {
                  show_raw_output = false;
                  return;
              }

              fc::buffered_istream buffered_argument_stream(argument_stream);

              bool command_is_valid = false;
              fc::variants arguments;
              try
              {
                arguments = _self->parse_interactive_command(buffered_argument_stream, command);
                // NOTE: arguments here have not been filtered for private keys or passwords
                // ilog( "command: ${c} ${a}", ("c",command)("a",arguments) ); 
                command_is_valid = true;
              }
              catch( const rpc::unknown_method& )
              {
                 if( command.size() )
                   *_out << "Error: invalid command \"" << command << "\"\n";
              }
              catch( const fc::canceled_exception&)
              {
                 throw;
              }
              catch( const fc::exception& e)
              {
                *_out << e.to_detail_string() <<"\n";
                *_out << "Error parsing command \"" << command << "\": " << e.to_string() << "\n";
                arguments = fc::variants { command };
                edump( (e.to_detail_string()) );
                auto usage = _client->help( command ); //_rpc_server->direct_invoke_method("help", arguments).as_string();
                *_out << usage << "\n";
              }

              //if command is valid, go ahead and execute it
              if (command_is_valid)
              {
                try
                {
                  fc::variant result = _self->execute_interactive_command(command, arguments);
                  _self->format_and_print_result(command, arguments, result);
                }
                catch( const fc::canceled_exception&)
                {
                  throw;
                }
                catch( const fc::exception& e )
                {
                  if( FILTER_OUTPUT_FOR_TESTS )
                    *_out << "Command failed with exception: " << e.to_string() << "\n";
                  else
                    *_out << e.to_detail_string() << "\n";
                }
              }
            } //parse_and_execute_interactive_command

            bool execute_command_line(const string& line)
            { try {
              string trimmed_line_to_parse(boost::algorithm::trim_copy(line));
              /** 
               *  On some OS X systems, std::stringstream gets corrupted and does not throw eof
               *  when expected while parsing the command.  Adding EOF (0x04) characater at the
               *  end of the string casues the JSON parser to recognize the EOF rather than relying
               *  on stringstream.  
               *
               *  @todo figure out how to fix things on these OS X systems.
               */
              trimmed_line_to_parse += string(" ") + char(0x04);
              if (!trimmed_line_to_parse.empty())
              {
                string::const_iterator iter = std::find_if(trimmed_line_to_parse.begin(), trimmed_line_to_parse.end(), ::isspace);
                string command;
                fc::istream_ptr argument_stream;
                if (iter != trimmed_line_to_parse.end())
                {
                  // then there are arguments to this function
                  size_t first_space_pos = iter - trimmed_line_to_parse.begin();
                  command = trimmed_line_to_parse.substr(0, first_space_pos);
                  argument_stream = std::make_shared<fc::stringstream>((trimmed_line_to_parse.substr(first_space_pos + 1)));
                }
                else
                {
                  command = trimmed_line_to_parse;
                  argument_stream = std::make_shared<fc::stringstream>();
                }
                try
                {
                  parse_and_execute_interactive_command(command,argument_stream);
                }
                catch( const fc::canceled_exception& )
                {
                  if( command == "quit" ) 
                    return false;
                  *_out << "Command aborted\n";
                }
              } //end if command line not empty
              return true;
            } FC_RETHROW_EXCEPTIONS( warn, "", ("command",line) ) }


            string get_line( const string& prompt = CLI_PROMPT_SUFFIX, bool no_echo = false)
            {
                  if( _quit ) return std::string();
                  if( _input_stream == nullptr )
                     FC_CAPTURE_AND_THROW( fc::canceled_exception ); //_input_stream != nullptr );

                  //FC_ASSERT( _self->is_interactive() );
                  string line;
                  if ( no_echo )
                  {
                      *_out << prompt;
                      // there is no need to add input to history when echo is off, so both Windows and Unix implementations are same
                      fc::set_console_echo(false);
                      _cin_thread.async([this,&line](){ std::getline( *_input_stream, line ); }, "getline").wait();
                      fc::set_console_echo(true);
                      *_out << std::endl;
                  }
                  else
                  {
                  #ifdef HAVE_READLINE 
                    if (_input_stream == &std::cin)
                    {
                      char* line_read = nullptr;
                      _out->flush(); //readline doesn't use cin, so we must manually flush _out
                      line_read = readline(prompt.c_str());
                      if(line_read && *line_read)
                          add_history(line_read);
                      if( line_read == nullptr )
                         FC_THROW_EXCEPTION( fc::eof_exception, "" );
                      line = line_read;
                      free(line_read);
                    }
                  else
                    {
                      *_out <<prompt;
                      _cin_thread.async([this,&line](){ std::getline( *_input_stream, line ); }, "getline" ).wait();
                    }
                  #else
                    *_out <<prompt;
                    _cin_thread.async([this,&line](){ std::getline( *_input_stream, line ); }, "getline").wait();
                  #endif
                  if (_input_stream_log)
                    {
                    _out->flush();
                    if (_saved_out)
                      *_input_stream_log << CLI_PROMPT_SUFFIX;
                    *_input_stream_log << line << std::endl;
                    }
                  }

                  boost::trim(line);
                  return line;
            }

            fc::variants parse_interactive_command(fc::buffered_istream& argument_stream, const string& command)
            {
              try
              {
                const bts::api::method_data& method_data = _rpc_server->get_method_data(command);
                return _self->parse_recognized_interactive_command(argument_stream, method_data);
              }
              catch( const fc::key_not_found_exception& )
              {
                return _self->parse_unrecognized_interactive_command(argument_stream, command);
              }
            }

            fc::variants parse_recognized_interactive_command( fc::buffered_istream& argument_stream,
                                                               const bts::api::method_data& method_data)
            { try {
              fc::variants arguments;
              for( unsigned i = 0; i < method_data.parameters.size(); ++i )
              {
                try
                {
                  arguments.push_back(_self->parse_argument_of_known_type(argument_stream, method_data, i));
                }
                catch( const fc::eof_exception&)
                {
                  if (method_data.parameters[i].classification != bts::api::required_positional)
                  {
                    return arguments;
                  }
                  else //if missing required argument, prompt for that argument
                  {
                    const bts::api::parameter_data& this_parameter = method_data.parameters[i];
                    string prompt = this_parameter.name /*+ "(" + this_parameter.type  + ")"*/ + ": ";

                    //if we're prompting for a password, don't echo it to console
                    bool is_new_passphrase = (this_parameter.type == "new_passphrase");
                    bool is_passphrase = (this_parameter.type == "passphrase") || is_new_passphrase;
                    if (is_passphrase)
                    {
                      auto passphrase = prompt_for_input( this_parameter.name, is_passphrase, is_new_passphrase );
                      arguments.push_back(fc::variant(passphrase));
                    }
                    else //not a passphrase
                    {
                      string prompt_answer = get_line(prompt, is_passphrase );
                      auto prompt_argument_stream = std::make_shared<fc::stringstream>(prompt_answer);
                      fc::buffered_istream buffered_argument_stream(prompt_argument_stream);
                      try
                      {
                        arguments.push_back(_self->parse_argument_of_known_type(buffered_argument_stream, method_data, i));
                      }
                      catch( const fc::eof_exception& e )
                      {
                          FC_THROW("Missing argument ${argument_number} of command \"${command}\"",
                                   ("argument_number", i + 1)("command", method_data.name)("cause",e.to_detail_string()) );
                      }
                      catch( fc::parse_error_exception& e )
                      {
                        FC_RETHROW_EXCEPTION(e, error, "Error parsing argument ${argument_number} of command \"${command}\": ${detail}",
                                              ("argument_number", i + 1)("command", method_data.name)("detail", e.get_log()));
                      }
                    } //else not a passphrase
                 } //end prompting for missing required argument
                } //end catch eof_exception
                catch( fc::parse_error_exception& e )
                {
                  FC_RETHROW_EXCEPTION(e, error, "Error parsing argument ${argument_number} of command \"${command}\": ${detail}",
                                        ("argument_number", i + 1)("command", method_data.name)("detail", e.get_log()));
                }

                if (method_data.parameters[i].classification == bts::api::optional_named)
                  break;
              }
              return arguments;
            } FC_CAPTURE_AND_RETHROW() }

            fc::variants parse_unrecognized_interactive_command( fc::buffered_istream& argument_stream,
                                                                 const string& command)
            {
              /* Quit isn't registered with the RPC server, the RPC server spells it "stop" */
              if (command == "quit")
                return fc::variants();

              FC_THROW_EXCEPTION(fc::key_not_found_exception, "Unknown command \"${command}\".", ("command", command));
            }

            fc::variant parse_argument_of_known_type( fc::buffered_istream& argument_stream,
                                                      const bts::api::method_data& method_data,
                                                      unsigned parameter_index)
            { try {
              const bts::api::parameter_data& this_parameter = method_data.parameters[parameter_index];
              if (this_parameter.type == "asset")
              {
                // for now, accept plain int, assume it's always in the base asset
                uint64_t amount_as_int;
                try
                {
                  fc::variant amount_as_variant = fc::json::from_stream(argument_stream);
                  amount_as_int = amount_as_variant.as_uint64();
                }
                catch( fc::bad_cast_exception& e )
                {
                  FC_RETHROW_EXCEPTION(e, error, "Error parsing argument ${argument_number} of command \"${command}\": ${detail}",
                                        ("argument_number", parameter_index + 1)("command", method_data.name)("detail", e.get_log()));
                }
                catch( fc::parse_error_exception& e )
                {
                  FC_RETHROW_EXCEPTION(e, error, "Error parsing argument ${argument_number} of command \"${command}\": ${detail}",
                                        ("argument_number", parameter_index + 1)("command", method_data.name)("detail", e.get_log()));
                }
                return fc::variant(bts::blockchain::asset(amount_as_int));
              }
              else if (this_parameter.type == "address")
              {
                // allow addresses to be un-quoted
                while( isspace(argument_stream.peek()) )
                  argument_stream.get();
                fc::stringstream address_stream;
                try
                {
                  while( !isspace(argument_stream.peek()) )
                    address_stream.put(argument_stream.get());
                }
                catch( const fc::eof_exception& )
                {
                   // *_out << "ignoring eof  line: "<<__LINE__<<"\n";
                   // expected and ignored
                }
                string address_string = address_stream.str();

                try
                {
                  bts::blockchain::address::is_valid(address_string);
                }
                catch( fc::exception& e )
                {
                  FC_RETHROW_EXCEPTION(e, error, "Error parsing argument ${argument_number} of command \"${command}\": ${detail}",
                                        ("argument_number", parameter_index + 1)("command", method_data.name)("detail", e.get_log()));
                }
                return fc::variant( bts::blockchain::address(address_string) );
              }
              else
              {
                // assume it's raw JSON
                try
                {
                  auto tmp = fc::json::from_stream( argument_stream );
                  return  tmp;
                }
                catch( fc::parse_error_exception& e )
                {
                  FC_RETHROW_EXCEPTION(e, error, "Error parsing argument ${argument_number} of command \"${command}\": ${detail}",
                                        ("argument_number", parameter_index + 1)("command", method_data.name)("detail", e.get_log()));
                }
              }
            } FC_RETHROW_EXCEPTIONS( warn, "", ("parameter_index",parameter_index) ) }

            fc::variant execute_interactive_command(const string& command, const fc::variants& arguments)
            {
              if( command == "quit" || command == "stop" || command == "exit" )
              {
                _quit = true;
                FC_THROW_EXCEPTION( fc::canceled_exception, "quit command issued" );
              }
              
              return execute_command( command, arguments );
            }

            fc::variant execute_command(const string& command, const fc::variants& arguments)
            { try {
              while (true)
              {
                // try to execute the method.  If the method needs the wallet to be
                // unlocked, it will throw an exception, and we'll attempt to
                // unlock it and then retry the command
                try
                {
                    return _rpc_server->direct_invoke_method(command, arguments);
                }
                catch( const rpc_wallet_open_needed_exception& )
                {
                    interactive_open_wallet();
                }
                catch( const rpc_wallet_unlock_needed_exception& )
                {
                    fc::istream_ptr unlock_time_arguments = std::make_shared<fc::stringstream>("300"); // default to five minute timeout
                    parse_and_execute_interactive_command( "wallet_unlock", unlock_time_arguments );
                }
              }
            } FC_RETHROW_EXCEPTIONS( warn, "", ("command",command) ) }

            string prompt_for_input( const string& prompt, bool no_echo = false, bool verify = false )
            { try {
                string input;
                while( true )
                {
                    input = get_line( prompt + ": ", no_echo );
                    if( input.empty() ) FC_THROW_EXCEPTION(fc::canceled_exception, "input aborted");

                    if( verify )
                    {
                        if( input != get_line( prompt + " (verify): ", no_echo ) )
                        {
                            *_out << "Input did not match, try again\n";
                            continue;
                        }
                    }
                    break;
                }
                return input;
            } FC_CAPTURE_AND_RETHROW( (prompt)(no_echo) ) }

            void interactive_open_wallet()
            {
              if( _client->get_wallet()->is_open() ) 
                return;

              *_out << "A wallet must be open to execute this command. You can:\n";
              *_out << "(o) Open an existing wallet\n";
              *_out << "(c) Create a new wallet\n";
              *_out << "(q) Abort command\n";

              string choice = get_line("Choose [o/c/q]: ");

              if (choice == "c")
              {
                fc::istream_ptr argument_stream = std::make_shared<fc::stringstream>();
                try
                {
                  parse_and_execute_interactive_command( "wallet_create", argument_stream );
                }
                catch( const fc::canceled_exception& )
                {
                }
              }
              else if (choice == "o")
              {
                fc::istream_ptr argument_stream = std::make_shared<fc::stringstream>();
                try
                {
                  parse_and_execute_interactive_command( "wallet_open", argument_stream );
                }
                catch( const fc::canceled_exception& )
                {
                }
              }
              else if (choice == "q")
              {
                FC_THROW_EXCEPTION(fc::canceled_exception, "");
              }
              else
              {
                  *_out << "Wrong answer!\n";
              }
            }

            void format_and_print_result(const string& command,
                                         const fc::variants arguments,
                                         const fc::variant& result)
            {
              string method_name = command;
              try
              {
                // command could be alias, so get the real name of the method.
                auto method_data = _rpc_server->get_method_data(command);
                method_name = method_data.name;
              }
              catch( const fc::key_not_found_exception& )
              {
                 elog( " KEY NOT FOUND " );
              }
              catch( ... )
              {
                 elog( " unexpected exception " );
              }

              if (show_raw_output)
              {
                  string result_type;
                  const bts::api::method_data& method_data = _rpc_server->get_method_data(method_name);
                  result_type = method_data.return_type;

                  if (result_type == "asset")
                  {
                    *_out << (string)result.as<bts::blockchain::asset>() << "\n";
                  }
                  else if (result_type == "address")
                  {
                    *_out << (string)result.as<bts::blockchain::address>() << "\n";
                  }
                  else if (result_type == "null" || result_type == "void")
                  {
                    *_out << "OK\n";
                  }
                  else
                  {
                    *_out << fc::json::to_pretty_string(result) << "\n";
                  }
              }

              if( !_out ) return;

              if (method_name == "help")
              {
                string help_string = result.as<string>();
                *_out << help_string << "\n";
              }
              else if( method_name == "wallet_account_vote_summary" )
              {
                  const auto& votes = result.as<account_vote_summary_type>();
                  *_out << pretty_vote_summary( votes, _client );
              }
              else if (method_name == "wallet_account_create")
              {
                  auto account_key = result.as_string();
                  auto account = _client->get_wallet()->get_account_for_address(public_key_type(account_key));

                  if (account)
                      *_out << "\n\nAccount created successfully. You may give the following link to others"
                               " to allow them to add you as a contact and send you funds:\n" CUSTOM_URL_SCHEME ":"
                            << account->name << ':' << account_key << '\n';
                  else
                      *_out << "Sorry, something went wrong when adding your account.\n";
              }
              else if (method_name == "debug_list_errors")
              {
                  auto error_map = result.as<map<fc::time_point,fc::exception> >();
                  if (error_map.empty())
                      *_out << "No errors.\n";
                  else
                    for( const auto& item : error_map )
                    {
                       (*_out) << string(item.first) << " (" << fc::get_approximate_relative_time_string( item.first ) << " )\n";
                       (*_out) << item.second.to_detail_string();
                       (*_out) << "\n";
                    }
              }
              else if( method_name == "get_info" )
              {
                  const auto& info = result.as<variant_object>();
                  *_out << pretty_info( info, _client );
              }
              else if( method_name == "blockchain_get_info" )
              {
                  const auto& config = result.as<variant_object>();
                  *_out << pretty_blockchain_info( config, _client );
              }
              else if( method_name == "wallet_get_info" )
              {
                  const auto& info = result.as<variant_object>();
                  *_out << pretty_wallet_info( info, _client );
              }
              else if (method_name == "wallet_account_transaction_history")
              {
                  const auto& transactions = result.as<vector<pretty_transaction>>();
                  *_out << pretty_transaction_list( transactions, _client );
              }
              else if( method_name == "wallet_market_order_list" )
              { try {
                  const auto& market_orders = result.as<vector<market_order>>();
                  *_out << pretty_market_orders( market_orders, _client );
              } FC_CAPTURE_AND_RETHROW() }
              else if( method_name == "blockchain_market_list_asks"
                       || method_name == "blockchain_market_list_bids"
                       || method_name == "blockchain_market_list_shorts" )
              {
                  const auto& market_orders = result.as<vector<market_order>>();
                  *_out << pretty_market_orders( market_orders, _client );
              }
              else if ( command == "wallet_list_my_accounts" )
              {
                  auto accts = result.as<vector<wallet_account_record>>();
                  print_receive_account_list( accts );
              }
              else if ( command == "wallet_list_accounts" || command == "wallet_list_unregistered_accounts" || command == "wallet_list_favorite_accounts" )
              {
                  auto accts = result.as<vector<wallet_account_record>>();
                  print_contact_account_list( accts );
              }
              else if (method_name == "wallet_account_balance" )
              {
                  const auto& balances = result.as<account_balance_summary_type>();
                  *_out << pretty_balances( balances, _client );
              }
              else if( method_name == "wallet_transfer"
                       || method_name == "wallet_get_transaction" )
              {
                  const auto& record = result.as<wallet_transaction_record>();
                  const auto& pretty = _client->get_wallet()->to_pretty_trx( record );
                  const std::vector<pretty_transaction> transactions = { pretty };
                  *_out << pretty_transaction_list( transactions, _client );
              }
              else if (method_name == "wallet_list")
              {
                  auto wallets = result.as<vector<string>>();
                  if (wallets.empty())
                      *_out << "No wallets found.\n";
                  else
                      for( const auto& wallet : wallets )
                          *_out << wallet << "\n";
              }
              else if (method_name == "network_get_usage_stats" )
              {
                  print_network_usage_stats(result.get_object());
              }
              else if( method_name == "blockchain_list_delegates"
                       || method_name == "blockchain_list_active_delegates" )
              {
                  const auto& delegate_records = result.as<vector<account_record>>();
                  *_out << pretty_delegate_list( delegate_records, _client );
              }
              else if (method_name == "blockchain_list_blocks")
              {
                  const auto& block_records = result.as<vector<block_record>>();
                  *_out << pretty_block_list( block_records, _client );
              }
              else if (method_name == "blockchain_list_accounts")
              {
                  string start = "";
                  int32_t count = 25; // In CLI this is a more sane default
                  if (arguments.size() > 0)
                      start = arguments[0].as_string();
                  if (arguments.size() > 1)
                      count = arguments[1].as<int32_t>();
                  print_registered_account_list( result.as<vector<account_record>>(), count );
              }
              else if (method_name == "blockchain_list_assets")
              {
                  const auto& asset_records = result.as<vector<asset_record>>();
                  *_out << pretty_asset_list( asset_records, _client );
              }
              else if (method_name == "blockchain_get_proposal_votes")
              {
                  auto votes = result.as<vector<proposal_vote>>();
                  *_out << std::left;
                  *_out << std::setw(15) << "DELEGATE";
                  *_out << std::setw(22) << "TIME";
                  *_out << std::setw(5)  << "VOTE";
                  *_out << std::setw(35) << "MESSAGE";
                  *_out << "\n----------------------------------------------------------------";
                  *_out << "-----------------------\n";
                  for( const auto& vote : votes )
                  {
                      auto rec = _client->get_chain()->get_account_record( vote.id.delegate_id );
                      *_out << std::setw(15) << pretty_shorten(rec->name, 14);
                      *_out << std::setw(20) << pretty_timestamp(vote.timestamp);
                      if (vote.vote == proposal_vote::no)
                      {
                          *_out << std::setw(5) << "NO";
                      }
                      else if (vote.vote == proposal_vote::yes)
                      {
                          *_out << std::setw(5) << "YES";
                      }
                      else
                      {
                          *_out << std::setw(5) << "??";
                      }
                      *_out << std::setw(35) << pretty_shorten(vote.message, 35);
                      *_out << "\n";
                  }
                  *_out << "\n";
              }
              else if( method_name == "blockchain_get_account" )
              {
                  const auto& account = result.as<oaccount_record>();
                  *_out << pretty_account( account, _client );
              }
              else if (method_name == "blockchain_list_forks")
              {
                  std::map<uint32_t, std::vector<fork_record>> forks = result.as<std::map<uint32_t, std::vector<fork_record>>>();
                  std::map<block_id_type, std::string> invalid_reasons; //Your reasons are invalid.

                  if (forks.empty())
                      *_out << "No forks.\n";
                  else
                  {
                      *_out << std::setw(15) << "FORKED BLOCK"
                            << std::setw(30) << "FORKING BLOCK ID"
                            << std::setw(30) << "SIGNING DELEGATE"
                            << std::setw(15) << "TXN COUNT"
                            << std::setw(10) << "SIZE"
                            << std::setw(20) << "TIMESTAMP"
                            << std::setw(10) << "LATENCY"
                            << std::setw(8)  << "VALID"
                            << std::setw(20)  << "IN CURRENT CHAIN"
                            << "\n" << std::string(158, '-') << "\n";

                      for( const auto& fork : forks )
                      {
                          *_out << std::setw(15) << fork.first << "\n";

                          for( const auto& tine : fork.second )
                          {
                              *_out << std::setw(45) << fc::variant(tine.block_id).as_string();

                              auto delegate_record = _client->get_chain()->get_account_record(tine.signing_delegate);
                              if (delegate_record.valid() && delegate_record->name.size() < 29)
                                  *_out << std::setw(30) << delegate_record->name;
                              else if (tine.signing_delegate > 0)
                                  *_out << std::setw(30) << std::string("Delegate ID ") + fc::variant(tine.signing_delegate).as_string();
                              else
                                  *_out << std::setw(30) << "Unknown Delegate";

                              *_out << std::setw(15) << tine.transaction_count
                                    << std::setw(10) << tine.size
                                    << std::setw(20) << pretty_timestamp(tine.timestamp)
                                    << std::setw(10) << tine.latency.to_seconds()
                                    << std::setw(8);

                              if (tine.is_valid.valid()) {
                                  if (*tine.is_valid) {
                                      *_out << "YES";
                                  }
                                  else {
                                      *_out << "NO";
                                      if (tine.invalid_reason.valid())
                                          invalid_reasons[tine.block_id] = tine.invalid_reason->to_detail_string();
                                      else
                                          invalid_reasons[tine.block_id] = "No reason given.";
                                  }
                              }
                              else
                                  *_out << "N/A";

                              *_out << std::setw(20);
                              if (tine.is_current_fork)
                                  *_out << "YES";
                              else
                                      *_out << "NO";

                              *_out << "\n";
                          }
                      }

                      if (invalid_reasons.size() > 0) {
                          *_out << "REASONS FOR INVALID BLOCKS\n";

                          for( const auto& excuse : invalid_reasons )
                              *_out << fc::variant(excuse.first).as_string() << ": " << excuse.second << "\n";
                      }
                  }
              }
              else if (method_name == "blockchain_list_pending_transactions")
              {
                  auto transactions = result.as<vector<signed_transaction>>();

                  if(transactions.empty())
                  {
                      *_out << "No pending transactions.\n";
                  }
                  else
                  {
                      *_out << std::setw(10) << "TXN ID"
                            << std::setw(10) << "SIZE"
                            << std::setw(25) << "OPERATION COUNT"
                            << std::setw(25) << "SIGNATURE COUNT"
                            << "\n" << std::string(70, '-') << "\n";

                      for( const auto& transaction : transactions )
                      {
                          if( FILTER_OUTPUT_FOR_TESTS )
                              *_out << std::setw(10) << "[redacted]";
                          else
                              *_out << std::setw(10) << transaction.id().str().substr(0, 8);

                          *_out << std::setw(10) << transaction.data_size()
                                << std::setw(25) << transaction.operations.size()
                                << std::setw(25) << transaction.signatures.size()
                                << "\n";
                      }
                  }
              }
              else if (method_name == "blockchain_list_proposals")
              {
                  auto proposals = result.as<vector<proposal_record>>();
                  *_out << std::left;
                  *_out << std::setw(10) << "ID";
                  *_out << std::setw(20) << "SUBMITTED BY";
                  *_out << std::setw(22) << "SUBMIT TIME";
                  *_out << std::setw(15) << "TYPE";
                  *_out << std::setw(20) << "SUBJECT";
                  *_out << std::setw(35) << "BODY";
                  *_out << std::setw(20) << "DATA";
                  *_out << std::setw(10)  << "RATIFIED";
                  *_out << "\n------------------------------------------------------------";
                  *_out << "-----------------------------------------------------------------";
                  *_out << "------------------\n";
                  for( const auto& prop : proposals )
                  {
                      *_out << std::setw(10) << prop.id;
                      auto delegate_rec = _client->get_chain()->get_account_record(prop.submitting_delegate_id);
                      *_out << std::setw(20) << pretty_shorten(delegate_rec->name, 19);
                      *_out << std::setw(20) << pretty_timestamp(prop.submission_date);
                      *_out << std::setw(15) << pretty_shorten(prop.proposal_type, 14);
                      *_out << std::setw(20) << pretty_shorten(prop.subject, 19);
                      *_out << std::setw(35) << pretty_shorten(prop.body, 34);
                      *_out << std::setw(20) << pretty_shorten(fc::json::to_pretty_string(prop.data), 19);
                      *_out << std::setw(10) << (prop.ratified ? "YES" : "NO");
                  }
                  *_out << "\n"; 
              }
              else if (method_name == "blockchain_market_order_book")
              {
                  auto bids_asks = result.as<std::pair<vector<market_order>,vector<market_order>>>();
                  if( bids_asks.first.size() == 0 && bids_asks.second.size() == 0 )
                  {
                     *_out << "No Orders\n";
                     return;
                  }
                  *_out << std::string(18, ' ') << "BIDS (* Short Order)" 
                        << std::string(38, ' ') << " | " 
                        << std::string(34, ' ') << "ASKS" 
                        << std::string(34, ' ') << "\n"
                        << std::left << std::setw(26) << "TOTAL" 
                        << std::setw(20) << "QUANTITY" 
                        << std::right << std::setw(30) << "PRICE"
                        << " | " << std::left << std::setw(30) << "PRICE" << std::right << std::setw(23) << "QUANTITY" << std::setw(26) << "TOTAL" <<"   COLLATERAL" << "\n"
                        << std::string(175, '-') << "\n";

                  vector<market_order>::iterator bid_itr = bids_asks.first.begin();
                  auto ask_itr = bids_asks.second.begin();

                  asset_id_type quote_id;
                  asset_id_type base_id;

                  if( bid_itr != bids_asks.first.end() )
                  {
                     quote_id = bid_itr->get_price().quote_asset_id;
                     base_id = bid_itr->get_price().base_asset_id;
                  }
                  if( ask_itr != bids_asks.second.end() )
                  {
                     quote_id = ask_itr->get_price().quote_asset_id;
                     base_id = ask_itr->get_price().base_asset_id;
                  }

                  auto quote_asset_record = _client->get_chain()->get_asset_record( quote_id );
                  // fee order is the market order to convert fees from other asset classes to XTS
                  bool show_fee_order_record = base_id == 0
                                               && !quote_asset_record->is_market_issued()
                                               && quote_asset_record->collected_fees > 0;

                  while( bid_itr != bids_asks.first.end() || ask_itr != bids_asks.second.end() )
                  {
                    if( show_fee_order_record )
                    {
                       *_out << std::left << std::setw(26) << _client->get_chain()->to_pretty_asset( asset(quote_asset_record->collected_fees, quote_id) )
                             << std::setw(20) << " "
                             << std::right << std::setw(30) << "MARKET PRICE";

                       *_out << ' ';
                       show_fee_order_record = false;
                    }
                    else if( bid_itr != bids_asks.first.end() )
                    {
                      *_out << std::left << std::setw(26) << (bid_itr->type == bts::blockchain::bid_order?
                                 _client->get_chain()->to_pretty_asset(bid_itr->get_balance())
                               : _client->get_chain()->to_pretty_asset(bid_itr->get_quote_quantity()))
                            << std::setw(20) << (bid_itr->type == bts::blockchain::bid_order?
                                 _client->get_chain()->to_pretty_asset(bid_itr->get_quantity())
                               : _client->get_chain()->to_pretty_asset(bid_itr->get_balance()))
                            << std::right << std::setw(30) << (fc::to_string(_client->get_chain()->to_pretty_price_double(bid_itr->get_price()) )+ " " + quote_asset_record->symbol);

                      if( bid_itr->type == bts::blockchain::short_order )
                          *_out << '*';
                      else
                          *_out << ' ';

                      ++bid_itr;
                    }
                    else
                      *_out << std::string(77, ' ');

                    *_out << "| ";

                    while( ask_itr != bids_asks.second.end() )
                    {
                      if( !ask_itr->collateral )
                      {
                         *_out << std::left << std::setw(30) << (fc::to_string(_client->get_chain()->to_pretty_price_double(ask_itr->get_price())) + " " + quote_asset_record->symbol)
                               << std::right << std::setw(23) << _client->get_chain()->to_pretty_asset(ask_itr->get_quantity())
                               << std::right << std::setw(26) << _client->get_chain()->to_pretty_asset(ask_itr->get_quote_quantity());
                          ++ask_itr;
                          break;
                      }
                      ++ask_itr;
                    }
                    *_out << "\n";
                  }

                  if( quote_asset_record->is_market_issued() && base_id == 0 )
                  {
                     *_out << std::string(175, '-') << "\n";
                     *_out << std::string(38, ' ') << " " 
                           << std::string(38, ' ') << "| " 
                           << std::string(34, ' ') << "MARGIN" 
                           << std::string(34, ' ') << "\n"
                           << std::left << std::setw(26) << " " 
                           << std::setw(20) << " " 
                           << std::right << std::setw(30) << " "
                           << " | " << std::left << std::setw(30) << "CALL PRICE" << std::right << std::setw(23) << "QUANTITY" << std::setw(26) << "TOTAL" <<"   COLLATERAL" << "\n"
                           << std::string(175, '-') << "\n";

                     {
                        auto ask_itr = bids_asks.second.rbegin();
                        while(  ask_itr != bids_asks.second.rend() )
                        {
                            if( ask_itr->collateral )
                            {
                                *_out << std::string(77, ' ');
                                *_out << "| ";
                                *_out << std::left << std::setw(30) << std::setprecision(8) << (fc::to_string(_client->get_chain()->to_pretty_price_double(ask_itr->get_price())) + " " + quote_asset_record->symbol)
                                     << std::right << std::setw(23) << _client->get_chain()->to_pretty_asset(ask_itr->get_quantity())
                                     << std::right << std::setw(26) << _client->get_chain()->to_pretty_asset(ask_itr->get_quote_quantity());
                                   *_out << "   " << _client->get_chain()->to_pretty_asset(asset(*ask_itr->collateral));
                                   *_out << "\n";
                            }
                           ++ask_itr;
                        }
                     }

                    auto recent_average_price = _client->get_chain()->get_market_status(quote_id, base_id)->avg_price_24h;
                    *_out << "Average Price in Recent Trades: "
                          << _client->get_chain()->to_pretty_price(recent_average_price)
                          << "     ";

                    auto status = _client->get_chain()->get_market_status( quote_id, base_id );
                    if( status )
                    {
                       auto maximum_short_price = recent_average_price;
                       maximum_short_price.ratio *= 4;
                       maximum_short_price.ratio /= 3;
                       auto minimum_cover_price = recent_average_price;
                       minimum_cover_price.ratio *= 2;
                       minimum_cover_price.ratio /= 3;
                       *_out << "Maximum Short Price: "
                             << _client->get_chain()->to_pretty_price( maximum_short_price )
                             <<"     ";
                       *_out << "Minimum Cover Price: "
                             << _client->get_chain()->to_pretty_price( minimum_cover_price )
                             <<"\n";

                       *_out << "Bid Depth: " << _client->get_chain()->to_pretty_asset( asset(status->bid_depth, base_id) ) <<"     ";
                       *_out << "Ask Depth: " << _client->get_chain()->to_pretty_asset( asset(status->ask_depth, base_id) ) <<"     ";
                       *_out << "Min Depth: " << _client->get_chain()->to_pretty_asset( asset(BTS_BLOCKCHAIN_MARKET_DEPTH_REQUIREMENT) ) <<"\n";
                       if(  status->last_error )
                       {
                          *_out << "Last Error:  ";
                          *_out << status->last_error->to_string() << "\n";
                          if( true || status->last_error->code() != 37005 /* insufficient funds */ )
                          {
                             *_out << "Details:\n";
                             *_out << status->last_error->to_detail_string() << "\n";
                          }
                       }
                    }

                  // TODO: print insurance fund for market issued assets

                  } // end call section that only applies to market issued assets vs XTS
                  else
                  {
                     auto status = _client->get_chain()->get_market_status( quote_id, base_id );
                     if(  status->last_error )
                     {
                        *_out << "Last Error:  ";
                        *_out << status->last_error->to_string() << "\n";
                        if( true || status->last_error->code() != 37005 /* insufficient funds */ )
                        {
                           *_out << "Details:\n";
                           *_out << status->last_error->to_detail_string() << "\n";
                        }
                     }
                  }

              }
              else if (method_name == "blockchain_market_order_history")
              {
                  vector<order_history_record> orders = result.as<vector<order_history_record>>();
                  if( orders.empty() )
                  {
                    *_out << "No Orders.\n";
                    return;
                  }

                  *_out << std::setw(7) << "TYPE"
                        << std::setw(30) << "PRICE"
                        << std::setw(25) << "PAID"
                        << std::setw(25) << "RECEIVED"
                        << std::setw(20) << "FEES"
                        << std::setw(23) << "TIMESTAMP"
                        << "\n" << std::string(130,'-') << "\n";

                  for( order_history_record order : orders )
                  {
                    *_out << std::setw(7) << "Buy"
                          << std::setw(30) << _client->get_chain()->to_pretty_price(order.bid_price)
                          << std::setw(25) << _client->get_chain()->to_pretty_asset(order.bid_paid)
                          << std::setw(25) << _client->get_chain()->to_pretty_asset(order.bid_received)
                          << std::setw(20) << _client->get_chain()->to_pretty_asset(order.bid_paid - order.ask_received)
                          << std::setw(23) << pretty_timestamp(order.timestamp)
                          << "\n"
                          << std::setw(7) << "Sell"
                          << std::setw(30) << _client->get_chain()->to_pretty_price(order.ask_price)
                          << std::setw(25) << _client->get_chain()->to_pretty_asset(order.ask_paid)
                          << std::setw(25) << _client->get_chain()->to_pretty_asset(order.ask_received)
                          << std::setw(20) << _client->get_chain()->to_pretty_asset(order.ask_paid - order.bid_received)
                          << std::setw(23) << pretty_timestamp(order.timestamp)
                          << "\n";
                  }
              }
              else if (method_name == "blockchain_market_price_history")
              {
                  market_history_points points = result.as<market_history_points>();
                  if( points.empty() )
                  {
                    *_out << "No price history.\n";
                    return;
                  }

                  *_out << std::setw(20) << "TIME"
                          << std::setw(20) << "HIGHEST BID"
                          << std::setw(20) << "LOWEST ASK"
                          << std::setw(20) << "TRADING VOLUME"
                          << std::setw(20) << "AVERAGE PRICE"
                          << "\n" << std::string(100,'-') << "\n";

                  for( auto point : points )
                  {
                    *_out << std::setw(20) << pretty_timestamp(point.timestamp)
                          << std::setw(20) << point.highest_bid
                          << std::setw(20) << point.lowest_ask
                          << std::setw(20) << _client->get_chain()->to_pretty_asset(asset(point.volume));
                    if(point.recent_average_price)
                      *_out << std::setw(20) << *point.recent_average_price;
                    else
                      *_out << std::setw(20) << "N/A";
                    *_out << "\n";
                  }
              }
              else if (method_name == "network_list_potential_peers")
              {
                  auto peers = result.as<std::vector<net::potential_peer_record>>();
                  *_out << std::setw(25) << "ENDPOINT";
                  *_out << std::setw(25) << "LAST SEEN";
                  *_out << std::setw(25) << "LAST CONNECT ATTEMPT";
                  *_out << std::setw(30) << "SUCCESSFUL CONNECT ATTEMPTS";
                  *_out << std::setw(30) << "FAILED CONNECT ATTEMPTS";
                  *_out << std::setw(35) << "LAST CONNECTION DISPOSITION";
                  *_out << std::setw(30) << "LAST ERROR";

                  *_out<< "\n";
                  for( const auto& peer : peers )
                  {
                      *_out<< std::setw(25) << string(peer.endpoint);
                      *_out<< std::setw(25) << pretty_timestamp(peer.last_seen_time);
                      *_out << std::setw(25) << pretty_timestamp(peer.last_connection_attempt_time);
                      *_out << std::setw(30) << peer.number_of_successful_connection_attempts;
                      *_out << std::setw(30) << peer.number_of_failed_connection_attempts;
                      *_out << std::setw(35) << string( peer.last_connection_disposition );
                      *_out << std::setw(30) << (peer.last_error ? peer.last_error->to_detail_string() : "none");

                      *_out << "\n";
                  }
              }
              else if( method_name == "wallet_set_transaction_fee" )
              {
                  const auto fee = result.as<asset>();
                  *_out << _client->get_chain()->to_pretty_asset( fee ) << "\n";
              }
              else
              {
                // there was no custom handler for this particular command, see if the return type
                // is one we know how to pretty-print
                string result_type;
                try
                {
                  const bts::api::method_data& method_data = _rpc_server->get_method_data(method_name);
                  result_type = method_data.return_type;

                  if (result_type == "asset")
                  {
                    *_out << (string)result.as<bts::blockchain::asset>() << "\n";
                  }
                  else if (result_type == "address")
                  {
                    *_out << (string)result.as<bts::blockchain::address>() << "\n";
                  }
                  else if (result_type == "null" || result_type == "void")
                  {
                    *_out << "OK\n";
                  }
                  else
                  {
                    *_out << fc::json::to_pretty_string(result) << "\n";
                  }
                }
                catch( const fc::key_not_found_exception& )
                {
                   elog( " KEY NOT FOUND " );
                   *_out << "key not found \n";
                }
                catch( ... )
                {
                   *_out << "unexpected exception \n";
                }
              }

              *_out << std::right; /* Ensure default alignment is restored */
            }

            void print_contact_account_list( const vector<wallet_account_record>& account_records )
            {
                *_out << std::setw( 35 ) << std::left << "NAME (* delegate)";
                *_out << std::setw( 64 ) << "KEY";
                *_out << std::setw( 22 ) << "REGISTERED";
                *_out << std::setw( 15 ) << "FAVORITE";
                *_out << std::setw( 15 ) << "APPROVAL";
                *_out << "\n";

                for( const auto& acct : account_records )
                {
                    if (acct.is_delegate())
                      *_out << std::setw(35) << pretty_shorten(acct.name, 33) + " *";
                    else
                      *_out << std::setw(35) << pretty_shorten(acct.name, 34);

                    *_out << std::setw(64) << string( acct.active_key() );

                    if( acct.id == 0 )
                      *_out << std::setw( 22 ) << "NO";
                    else 
                      *_out << std::setw( 22 ) << pretty_timestamp(acct.registration_date);

                    if( acct.is_favorite )
                      *_out << std::setw( 15 ) << "YES";
                    else
                      *_out << std::setw( 15 ) << "NO";

                    *_out << std::setw( 10 ) << std::to_string( acct.approved );
                    *_out << "\n";
                }
            }

            void print_receive_account_list( const vector<wallet_account_record>& account_records )
            {
                *_out << std::setw( 35 ) << std::left << "NAME (* delegate)";
                *_out << std::setw( 64 ) << "KEY";
                *_out << std::setw( 22 ) << "REGISTERED";
                *_out << std::setw( 15 ) << "FAVORITE";
                *_out << std::setw( 15 ) << "APPROVAL";
                *_out << std::setw( 25 ) << "BLOCK PRODUCTION ENABLED";
                *_out << "\n";

                for( const auto& acct : account_records )
                {
                    if (acct.is_delegate())
                    {
                        *_out << std::setw(35) << pretty_shorten(acct.name, 33) + " *";
                    }
                    else
                    {
                        *_out << std::setw(35) << pretty_shorten(acct.name, 34);
                    }

                    *_out << std::setw(64) << string( acct.active_key() );

                    if (acct.id == 0 ) 
                    {
                        *_out << std::setw( 22 ) << "NO";
                    } 
                    else 
                    {
                        *_out << std::setw( 22 ) << pretty_timestamp(acct.registration_date);
                    }

                    if( acct.is_favorite )
                      *_out << std::setw( 15 ) << "YES";
                    else
                      *_out << std::setw( 15 ) << "NO";

                    *_out << std::setw( 15 ) << std::to_string( acct.approved );
                    if (acct.is_delegate())
                        *_out << std::setw( 25 ) << (acct.block_production_enabled ? "YES" : "NO");
                    else
                        *_out << std::setw( 25 ) << "N/A";
                    *_out << "\n";
                }
            }

            void print_registered_account_list( const vector<account_record>& account_records, int32_t count )
            {
                *_out << std::setw( 35 ) << std::left << "NAME (* delegate)";
                *_out << std::setw( 64 ) << "KEY";
                *_out << std::setw( 22 ) << "REGISTERED";
                *_out << std::setw( 15 ) << "VOTES FOR";
                *_out << std::setw( 15 ) << "APPROVAL";

                *_out << '\n';
                for( int i = 0; i < 151; ++i )
                    *_out << '-';
                *_out << '\n';

                auto counter = 0;
                for( const auto& acct : account_records )
                {
                    if (acct.is_delegate())
                    {
                        *_out << std::setw(35) << pretty_shorten(acct.name, 33) + " *";
                    }
                    else
                    {
                        *_out << std::setw(35) << pretty_shorten(acct.name, 34);
                    }
                    
                    *_out << std::setw(64) << string( acct.active_key() );
                    *_out << std::setw( 22 ) << pretty_timestamp(acct.registration_date);

                    if ( acct.is_delegate() )
                    {
                        *_out << std::setw(15) << acct.delegate_info->votes_for;

                        try
                        {
                            *_out << std::setw( 15 ) << std::to_string( _client->get_wallet()->get_account_approval( acct.name ) );
                        }
                        catch( ... )
                        {
                            *_out << std::setw(15) << false;
                        }
                    }
                    else
                    {
                        *_out << std::setw(15) << "N/A";
                        *_out << std::setw(15) << "N/A";
                    }

                    *_out << "\n";

                    /* Count is positive b/c CLI overrides default -1 arg */
                    if (counter >= count)
                    {
                        *_out << "... Use \"blockchain_list_accounts <start_name> <count>\" to see more.\n";
                        return;
                    }
                    counter++;

                }
            }

            void print_network_usage_graph( const std::vector<uint32_t>& usage_data )
            {
              uint32_t max_value = *boost::max_element(usage_data);
              uint32_t min_value = *boost::min_element(usage_data);
              const unsigned num_rows = 10;
              for( unsigned row = 0; row < num_rows; ++row )
              {
                uint32_t threshold = min_value + (max_value - min_value) / (num_rows - 1) * (num_rows - row - 1);
                for( unsigned column = 0; column < usage_data.size(); ++column )
                  (*_out) << (usage_data[column] >= threshold ?  "*" : " ");
                (*_out) << " " << threshold << " bytes/sec\n";
              }
              (*_out)  << "\n";
            }

            void print_network_usage_stats( const fc::variant_object& stats )
            {
              std::vector<uint32_t> usage_by_second = stats["usage_by_second"].as<std::vector<uint32_t> >();
              if (!usage_by_second.empty())
              {
                (*_out) << "last minute:\n";
                print_network_usage_graph(usage_by_second);
                (*_out)  << "\n";
              }
              std::vector<uint32_t> usage_by_minute = stats["usage_by_minute"].as<std::vector<uint32_t> >();
              if (!usage_by_minute.empty())
              {
                (*_out) << "last hour:\n";
                print_network_usage_graph(usage_by_minute);
                (*_out)  << "\n";
              }
              std::vector<uint32_t> usage_by_hour = stats["usage_by_hour"].as<std::vector<uint32_t> >();
              if (!usage_by_hour.empty())
              {
                (*_out) << "by hour:\n";
                print_network_usage_graph(usage_by_hour);
                (*_out)  << "\n";
              }
            }

            void display_status_message(const std::string& message);
#ifdef HAVE_READLINE
            typedef std::map<string, bts::api::method_data> method_data_map_type;
            method_data_map_type _method_data_map;
            typedef std::map<string, string>  method_alias_map_type;
            method_alias_map_type _method_alias_map;
            method_alias_map_type::iterator _command_completion_generator_iter;
            bool _method_data_is_initialized;
            void initialize_method_data_if_necessary();
            char* json_command_completion_generator(const char* text, int state);
            char* json_argument_completion_generator(const char* text, int state);
            char** json_completion(const char* text, int start, int end);
#endif
      };

#ifdef HAVE_READLINE
    static cli_impl* cli_impl_instance = NULL;
    extern "C" char** json_completion_function(const char* text, int start, int end);
    extern "C" int control_c_handler(int count, int key);
    extern "C" int get_character(FILE* stream);
#endif

    cli_impl::cli_impl(bts::client::client* client, std::istream* command_script, std::ostream* output_stream)
    :_client(client)
    ,_rpc_server(client->get_rpc_server()),
    _cin_thread("stdin_reader")
    ,_quit(false)
    ,show_raw_output(false)
    ,_daemon_mode(false)
    ,nullstream(boost::iostreams::null_sink())
    , _saved_out(nullptr)
    ,_out(output_stream ? output_stream : &nullstream)
    ,_command_script(command_script)
    {
#ifdef HAVE_READLINE
      //if( &output_stream == &std::cout ) // readline
      {
         cli_impl_instance = this;
         _method_data_is_initialized = false;
         rl_attempted_completion_function = &json_completion_function;
         rl_getc_function = &get_character;
      }
#ifndef __APPLE__
      // TODO: find out why this isn't defined on APPL
      //rl_bind_keyseq("\\C-c", &control_c_handler);
#endif
#endif
    }

#ifdef HAVE_READLINE
    void cli_impl::initialize_method_data_if_necessary()
    {
      if (!_method_data_is_initialized)
      {
        _method_data_is_initialized = true;
        vector<bts::api::method_data> method_data_list = _rpc_server->get_all_method_data();
        for( const auto& method_data : method_data_list )
        {
          _method_data_map[method_data.name] = method_data;
          _method_alias_map[method_data.name] = method_data.name;
          for( const auto& alias : method_data.aliases )
            _method_alias_map[alias] = method_data.name;
        }
      }
    }
    extern "C" int get_character(FILE* stream)
    {
      return cli_impl_instance->_cin_thread.async([stream](){ return rl_getc(stream); }, "rl_getc").wait();
    }
    extern "C" char* json_command_completion_generator_function(const char* text, int state)
    {
      return cli_impl_instance->json_command_completion_generator(text, state);
    }
    extern "C" char* json_argument_completion_generator_function(const char* text, int state)
    {
      return cli_impl_instance->json_argument_completion_generator(text, state);
    }
    extern "C" char** json_completion_function(const char* text, int start, int end)
    {
      return cli_impl_instance->json_completion(text, start, end);
    }
    extern "C" int control_c_handler(int count, int key)
    {
       std::cout << "\n\ncontrol-c!\n\n";
      return 0;
    }

    // implement json command completion (for function names only)
    char* cli_impl::json_command_completion_generator(const char* text, int state)
    {
      initialize_method_data_if_necessary();

      if (state == 0)
        _command_completion_generator_iter = _method_alias_map.lower_bound(text);
      else
        ++_command_completion_generator_iter;

      while (_command_completion_generator_iter != _method_alias_map.end())
      {
        if (_command_completion_generator_iter->first.compare(0, strlen(text), text) != 0)
          break; // no more matches starting with this prefix

        if (_command_completion_generator_iter->second == _command_completion_generator_iter->first) // suppress completing aliases
          return strdup(_command_completion_generator_iter->second.c_str());
        else
          ++_command_completion_generator_iter;  
      }

      rl_attempted_completion_over = 1; // suppress default filename completion
      return 0;
    }
    char* cli_impl::json_argument_completion_generator(const char* text, int state)
    {
      rl_attempted_completion_over = 1; // suppress default filename completion
      return 0;
    }
    char** cli_impl::json_completion(const char* text, int start, int end)
    {
      if (start == 0) // beginning of line, match a command
        return rl_completion_matches(text, &json_command_completion_generator_function);
      else
      {
        // not the beginning of a line.  figure out what the type of this argument is 
        // and whether we can complete it.  First, look up the method
        string command_line_to_parse(rl_line_buffer, start);
        string trimmed_command_to_parse(boost::algorithm::trim_copy(command_line_to_parse));
        
        if (!trimmed_command_to_parse.empty())
        {
          auto alias_iter = _method_alias_map.find(trimmed_command_to_parse);
          if (alias_iter != _method_alias_map.end())
          {
            auto method_data_iter = _method_data_map.find(alias_iter->second);
            if (method_data_iter != _method_data_map.end())
            {
            }
          }
          try
          {
            const bts::api::method_data& method_data = cli_impl_instance->_rpc_server->get_method_data(trimmed_command_to_parse);
            if (method_data.name == "help")
            {
                return rl_completion_matches(text, &json_command_completion_generator_function);
            }
          }
          catch( const bts::rpc::unknown_method& )
          {
            // do nothing
          }
        }
        
        return rl_completion_matches(text, &json_argument_completion_generator_function);
      }
    }

#endif
    void cli_impl::display_status_message(const std::string& message)
    {
      if( !_input_stream || !_out || _daemon_mode ) 
        return;
#ifdef HAVE_READLINE
      if (rl_prompt)
      {
        char* saved_line = rl_copy_text(0, rl_end);
        char* saved_prompt = strdup(rl_prompt);
        int saved_point = rl_point;
        rl_set_prompt("");
        rl_replace_line("", 0);
        rl_redisplay();
        (*_out) << message << "\n";
        _out->flush();
        rl_set_prompt(saved_prompt);
        rl_replace_line(saved_line, 0);
        rl_point = saved_point;
        rl_redisplay();
        free(saved_line);
        free(saved_prompt);
      }
      else
      {
        (*_out) << message << "\n";
        // it's not clear what state we're in if rl_prompt is null, but we've had reports
        // of crashes.  Just swallow the message and avoid crashing.
      }
#else
      // not supported; no way for us to restore the prompt, so just swallow the message
#endif
    }

    void cli_impl::process_commands(std::istream* input_stream)
    {  try {
      FC_ASSERT( input_stream != nullptr );
      _input_stream = input_stream;
      //force flushing to console and log file whenever input is read
      _input_stream->tie( _out );
      string line = get_line(get_prompt());
      while (_input_stream->good() && !_quit )
      {
        if (!execute_command_line(line))
          break;
        if( !_quit )
          line = get_line( get_prompt() );
      } // while cin.good
      wlog( "process commands exiting" );
    }  FC_CAPTURE_AND_RETHROW() }

  } // end namespace detail

   cli::cli( bts::client::client* client, std::istream* command_script, std::ostream* output_stream)
  :my( new detail::cli_impl(client,command_script,output_stream) )
  {
    my->_self = this;
  }

  void cli::set_input_stream_log(boost::optional<std::ostream&> input_stream_log)
  {
    my->_input_stream_log = input_stream_log;
  } 

  //disable reading from std::cin
  void cli::set_daemon_mode(bool daemon_mode) { my->_daemon_mode = daemon_mode; }
 
  void cli::display_status_message(const std::string& message)
  {
    if (my)
      my->display_status_message(message);
  }
 
  void cli::process_commands(std::istream* input_stream)
  {
    ilog( "starting to process interactive commands" );
    my->process_commands(input_stream);
  }

  cli::~cli()
  {
    try
    {
      wait_till_cli_shutdown();
    }
    catch( const fc::exception& e )
    {
      wlog( "${e}", ("e",e.to_detail_string()) );
    }
  }

  void cli::start() { my->start(); }

  void cli::wait_till_cli_shutdown()
  {
     ilog( "waiting on server to quit" );
     my->_rpc_server->close();
     my->_rpc_server->wait_till_rpc_server_shutdown();
     ilog( "rpc server shut down" );
  }

  void cli::enable_output(bool enable_output)
  {
    if (!enable_output)
    { //save off original output and set output to nullstream
      my->_saved_out = my->_out;
      my->_out = &my->nullstream;
    }
    else
    { //can only enable output if it was previously disabled
      if (my->_saved_out)
      {
        my->_out = my->_saved_out;
        my->_saved_out = nullptr;
      }
    }
    
  }

  void cli::filter_output_for_tests(bool enable_flag)
  {
    FILTER_OUTPUT_FOR_TESTS = enable_flag;
  }

  bool cli::execute_command_line( const string& line, std::ostream* output)
  {
    auto old_output = my->_out;
    auto old_input = my->_input_stream;
    bool result = false;
    if( output != &my->nullstream )
    {
        my->_out = output;
        my->_input_stream = nullptr;
    }
    result = my->execute_command_line(line);
    if( output != &my->nullstream)
    {
        my->_out = old_output;
        my->_input_stream = old_input;
    }
    return result;
  }

  fc::variant cli::parse_argument_of_known_type(fc::buffered_istream& argument_stream,
                                                const bts::api::method_data& method_data,
                                                unsigned parameter_index)
  {
    return my->parse_argument_of_known_type(argument_stream, method_data, parameter_index);
  }
  fc::variants cli::parse_unrecognized_interactive_command(fc::buffered_istream& argument_stream,
                                                           const string& command)
  {
    return my->parse_unrecognized_interactive_command(argument_stream, command);
  }
  fc::variants cli::parse_recognized_interactive_command(fc::buffered_istream& argument_stream,
                                                         const bts::api::method_data& method_data)
  {
    return my->parse_recognized_interactive_command(argument_stream, method_data);
  }
  fc::variants cli::parse_interactive_command(fc::buffered_istream& argument_stream, const string& command)
  {
    return my->parse_interactive_command(argument_stream, command);
  }
  fc::variant cli::execute_interactive_command(const string& command, const fc::variants& arguments)
  {
    return my->execute_interactive_command(command, arguments);
  }
  void cli::format_and_print_result(const string& command,
                                    const fc::variants& arguments,
                                    const fc::variant& result)
  {
    return my->format_and_print_result(command, arguments, result);
  }

} } // bts::cli
