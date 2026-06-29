#!/bin/bash
#
# Test that modified program runs identically to base version
# commpiled without -ffast-math. It's dangerous

# If detailed outout is symlink make sure it exists
touch detailed-output.json
./main test/conf.json test/test_out.json | grep -E '^t=' > test/test_stdout.txt

RES=0
if [ "$(sha1sum < test/test_stdout.txt)" != "$(sha1sum < test/stdout.txt)" ]; then
    echo "==== STDOUT MISMATCH ===="
    diff -u test/test_stdout.txt test/stdout.txt
    RES=1
fi

if [ "$(sha1sum < test/test_out.json)" != "$(sha1sum < test/out.json)" ]; then
    echo "==== OUTPUT JSON MISMATCH ===="
    diff -u <(jq < test/test_out.json) <(jq < test/out.json)
    RES=1
fi

exit $RES
