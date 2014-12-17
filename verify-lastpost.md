
How to verify the legitimacy of this post
-----------------------------------------

Download and verify the file:

    wget -O - https://raw.githubusercontent.com/drltc/bitshares_toolkit/drltc-lastpost/drltc-lastpost.md | tee drltc-lastpost.md | sha256sum

You should verify the content of `drltc-lastpost.md` created by this command matches the forum post, and the sha256sum will be the following hash:

    489af05fa35a928226294e70b7c458af256479310f70c223c894392fec6b4b7e

Put the hash into this command in any BitShares client:

    blockchain_verify_signature drltc "489af05fa35a928226294e70b7c458af256479310f70c223c894392fec6b4b7e" "1f60baa9b38e1bf4b471700f59f174d09c84ca353a3cd5c674c282eeac31cc8d6054e66ca3b4b5a215b524e23d5067fd04a6ed4fc6072d93097f9dfdf7bfaa9918"

The command returns `true` indicating that the owner of the `drltc` blockchain account has signed the content of this post (the really long hex string starting with `1f` is the signature data).

