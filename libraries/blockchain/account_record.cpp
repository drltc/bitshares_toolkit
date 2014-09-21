#include <bts/blockchain/account_record.hpp>

#include <fc/exception/exception.hpp>

namespace bts { namespace blockchain {

    // Some of these might be in the genesis block. This is only checked when new accounts are registered.
    std::set<string> banned_names = {
        "ac", "academy", "accountants", "active", "actor", "ad", "ae", "aero", "af", "ag",
        "agency", "ai", "airforce", "al", "am", "an", "ao", "aq", "ar", "archi", "army",
        "arpa", "as", "asia", "associates", "at", "attorney", "au", "auction", "audio", "autos", "aw",
        "ax", "axa", "az", "ba", "bar", "bargains", "bayern", "bb", "bd", "be", "beer",
        "berlin", "best", "bf", "bg", "bh", "bi", "bid", "bike", "bio", "biz",
        "bj", "black", "blackfriday", "blue", "bm", "bmw", "bn", "bnpparibas", "bo", "boo",
        "boutique", "br", "brussels", "bs", "bt", "build", "builders", "business", "buzz", "bv",
        "bw", "by", "bz", "bzh", "ca", "cab", "cal", "camera", "camp", "cancerresearch",
        "capetown", "capital", "caravan", "cards", "care", "career", "careers", "cash", "cat", "catering",
        "cc", "cd", "center", "ceo", "cern", "cf", "cg", "ch", "channel", "cheap",
        "christmas", "chrome", "church", "ci", "citic", "city", "ck", "cl", "claims", "cleaning",
        "click", "clinic", "clothing", "club", "cm", "cn", "co", "codes", "coffee", "college", "cologne",
        "com", "community", "company", "computer", "condos", "construction", "consulting", "contractors", "cooking", "cool",
        "coop", "country", "cr", "credit", "creditcard", "cruises", "cu", "cuisinella", "cv", "cw",
        "cx", "cy", "cymru", "cz", "dad", "dance", "dating", "day", "de", "deals",
        "degree", "democrat", "dental", "dentist", "desi", "diamonds", "diet", "digital", "direct", "directory",
        "discount", "dj", "dk", "dm", "dnp", "do", "domains", "durban", "dz", "eat",
        "ec", "edu", "education", "ee", "eg", "email", "engineer", "engineering", "enterprises", "equipment",
        "er", "es", "esq", "estate", "et", "eu", "eus", "events", "exchange", "expert",
        "exposed", "fail", "farm", "feedback", "fi", "finance", "financial", "fish", "fishing", "fitness",
        "fj", "fk", "flights", "florist", "fly", "fm", "fo", "foo", "foundation", "fr",
        "frl", "frogans", "fund", "furniture", "futbol", "ga", "gal", "gallery", "gb", "gbiz",
        "gd", "ge", "gent", "gf", "gg", "gh", "gi", "gift", "gifts", "gives",
        "gl", "glass", "gle", "global", "globo", "gm", "gmail", "gmo", "gmx", "gn",
        "google", "gop", "gov", "gp", "gq", "gr", "graphics", "gratis", "green", "gripe",
        "gs", "gt", "gu", "guide", "guitars", "guru", "gw", "gy", "hamburg", "haus",
        "healthcare", "help", "here", "hiphop", "hiv", "hk", "hm", "hn", "holdings", "holiday",
        "homes", "horse", "host", "hosting", "house", "how", "hr", "ht", "hu", "id",
        "ie", "il", "im", "immo", "immobilien", "in", "industries", "info", "ing", "ink",
        "institute", "insure", "int", "international", "investments", "io", "iq", "ir", "is", "it",
        "je", "jetzt", "jm", "jo", "jobs", "joburg", "jp", "juegos", "kaufen", "ke",
        "kg", "kh", "ki", "kim", "kitchen", "kiwi", "km", "kn", "koeln", "kp",
        "kr", "krd", "kred", "kw", "ky", "kz", "la", "lacaixa", "land", "lawyer",
        "lb", "lc", "lease", "lgbt", "li", "life", "lighting", "limited", "limo", "link",
        "lk", "loans", "london", "lotto", "lr", "ls", "lt", "ltda", "lu", "luxe",
        "luxury", "lv", "ly", "ma", "maison", "management", "mango", "market", "marketing", "mc",
        "md", "me", "media", "meet", "melbourne", "meme", "menu", "mg", "mh", "miami",
        "mil", "mini", "mk", "ml", "mm", "mn", "mo", "mobi", "moda", "moe",
        "monash", "mortgage", "moscow", "motorcycles", "mov", "mp", "mq", "mr", "ms", "mt",
        "mu", "museum", "mv", "mw", "mx", "my", "mz", "na", "nagoya", "name",
        "navy", "nc", "ne", "net", "network", "neustar", "new", "nexus", "nf", "ng",
        "ngo", "nhk", "ni", "ninja", "nl", "no", "np", "nr", "nra", "nrw",
        "nu", "nyc", "nz", "okinawa", "om", "ong", "onl", "ooo", "org", "organic",
        "otsuka", "ovh", "pa", "paris", "partners", "parts", "pe", "pf", "pg", "ph",
        "pharmacy", "photo", "photography", "photos", "physio", "pics", "pictures", "pink", "pizza", "pk",
        "pl", "place", "plumbing", "pm", "pn", "post", "pr", "praxi", "press", "pro",
        "prod", "productions", "prof", "properties", "property", "ps", "pt", "pub", "pw", "py",
        "qa", "qpon", "quebec", "re", "realtor", "recipes", "red", "rehab", "reise", "reisen",
        "ren", "rentals", "repair", "report", "republican", "rest", "restaurant", "reviews", "rich", "rio",
        "ro", "rocks", "rodeo", "rs", "rsvp", "ru", "ruhr", "rw", "ryukyu", "sa",
        "saarland", "sarl", "sb", "sc", "sca", "scb", "schmidt", "schule", "scot", "sd",
        "se", "services", "sexy", "sg", "sh", "shiksha", "shoes", "si", "singles", "sj",
        "sk", "sl", "sm", "sn", "so", "social", "software", "sohu", "solar", "solutions",
        "soy", "space", "spiegel", "sr", "st", "su", "supplies", "supply", "support", "surf",
        "surgery", "suzuki", "sv", "sx", "sy", "systems", "sz", "tatar", "tattoo", "tax",
        "tc", "td", "technology", "tel", "tf", "tg", "th", "tienda", "tips", "tirol",
        "tj", "tk", "tl", "tm", "tn", "to", "today", "tokyo", "tools", "top",
        "town", "toys", "tp", "tr", "trade", "training", "travel", "tt", "tv", "tw",
        "tz", "ua", "ug", "uk", "university", "uno", "uol", "us", "uy", "uz",
        "va", "vacations", "vc", "ve", "vegas", "ventures", "versicherung", "vet", "vg", "vi",
        "viajes", "villas", "vision", "vlaanderen", "vn", "vodka", "vote", "voting", "voto", "voyage",
        "vu", "wales", "wang", "watch", "webcam", "website", "wed", "wf", "whoswho", "wien",
        "wiki", "williamhill", "wme", "works", "world", "ws", "wtc", "wtf",
        "xxx", "xyz", "yachts", "yandex", "ye", "yokohama", "youtube", "yt", "za", "zip", "zm", "zone", "zw",
        "local", "localhost"
    };

    bool account_record::is_null()const
    {
        return owner_key == public_key_type();
    }

    account_record account_record::make_null()const
    {
        account_record cpy(*this);
        cpy.owner_key = public_key_type();
        return cpy;
    }

    share_type account_record::delegate_pay_balance()const
    {
        FC_ASSERT( is_delegate() );
        return delegate_info->pay_balance;
    }

    bool account_record::is_delegate()const
    {
        return !!delegate_info;
    }

    int64_t account_record::net_votes()const
    {
        FC_ASSERT( is_delegate() );
        return delegate_info->votes_for;
    }

    void account_record::adjust_votes_for( share_type delta )
    {
        FC_ASSERT( is_delegate() );
        delegate_info->votes_for += delta;
    }

    bool account_record::is_retracted()const
    {
        return active_key() == public_key_type();
    }

    address account_record::active_address()const
    {
        return address(active_key());
    }

    void account_record::set_active_key( const time_point_sec& now, const public_key_type& new_key )
    { try {
        FC_ASSERT( now != fc::time_point_sec() );
        active_key_history[now] = new_key;
    } FC_CAPTURE_AND_RETHROW( (now)(new_key) ) }

    public_key_type account_record::active_key()const
    {
        if( active_key_history.size() )
            return active_key_history.rbegin()->second;

        return public_key_type();
    }

    uint8_t account_record::delegate_pay_rate()const
    {
        if( is_delegate() ) return delegate_info->pay_rate;
        return 255;
    }

}} // bts::blockchain
