# Test certificates — the private keys here are public on purpose

Everything in this directory is a throwaway fixture for `tests/hw/test_tls_server.py`.
The `.key` files are real private keys and they are committed deliberately: the
test needs both halves of a key pair on both sides of a connection, and
generating them at test time would mean the sketch's baked-in `src/certs.h`
could not match.

**They protect nothing and secure nothing.** They name `ch32h4.local` and a
CA called "ch32h4 test client CA", neither of which exists anywhere. Do not
copy them into anything real — `make_certs.sh` makes a fresh set in about a
second, and the same commands are what you want for your own board.

Regenerate with `sh make_certs.sh`, which also rewrites `../src/certs.h`.
Both the PEM files and the header must be committed together: the test reads
these files while the board answers from the header, so if the two drift apart
every handshake in the suite fails.
