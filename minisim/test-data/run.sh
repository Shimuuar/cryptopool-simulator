#!/bin/bash
#
# Test that modified program runs identically to base version
# commpiled without -ffast-math. It's dangerous

# If detailed outout is symlink make sure it exists
touch detailed-output.json
./main test-data/conf.json test-data/test_out.json | grep -E '^t=' > test-data/test_stdout.txt
if [ $? != 0 ]; then
   exit 1
fi
RES=0
if [ "$(sha1sum < test-data/test_stdout.txt)" != "$(sha1sum < test-data/stdout.txt)" ]; then
    echo "==== STDOUT MISMATCH ===="
    diff -u test-data/stdout.txt test-data/test_stdout.txt
    RES=1
fi

if [ "$(sha1sum < test-data/test_out.json)" != "$(sha1sum < test-data/out.json)" ]; then
    echo "==== OUTPUT JSON MISMATCH ===="
    diff -u <(jq < test-data/out.json) <(jq < test-data/test_out.json)
    RES=1
fi

exit $RES
