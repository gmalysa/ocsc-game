#!/bin/bash

# Parse a log file into a string representing the people and their resolutions
cat $1 | grep -E "(new person received|accept=)" | sed -e '/0 attributes/ca' -e '/attributes: 0$/cb' -e '/attributes: 1$/cc' -e '/attributes: 1 0/cd' -e '/true/ctrue' -e '/false/cfalse' \
| sed -e 'N;s/a\ntrue/A/;s/a\nfalse/a/;s/b\ntrue/B/;s/b\nfalse/b/;s/c\ntrue/C/;s/c\nfalse/C/;s/d\ntrue/D/;s/d\nfalse/d/' | tr -d '\n'
