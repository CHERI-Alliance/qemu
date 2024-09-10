#!/usr/bin/env python
# Parse inputfile foe test name and pass fail strings, and generate junit style
# output
# this should be replaced with something more robust but is sufficient for now

import sys
import subprocess

testcases=[]
passes = 0
fails = 0
with open(sys.argv[1]) as commandfile:
    commands = [line.rstrip() for line in commandfile]
    for command in commands:
        commandline=command.split(' ')
        
        test_name = commandline[0]
        result=None
        # due to different behaviours in the subprocess module by python version
        # we need to handle this differently
        if sys.version_info >= (3,7,0):
            result = subprocess.run(commandline[1:],capture_output=True,text=True)
        elif sys.version_info >= (3,6,8):
            result = subprocess.run(commandline[1:],stdout=subprocess.PIPE)
        else:
            print("Unable to handle python version <3.6.8")
            exit()
        if result.returncode == 0:
            passes = passes+1
            testcases.append(f'<testcase name="{test_name}" classname="Tests.{test_name}">' + \
                f'<system-out>{result.stdout}</system-out>\n' + \
                    "</testcase>")
        else:
            fails = fails+1
            testcases.append(f'<testcase name="{test_name}" classname="Tests.{test_name}">' + \
                f'<system-out>{result.stdout}</system-out>\n' + \
                 f'<failure message="Test returned failure" type="AssertionError">             </failure> ' + \
                    "</testcase>")
    
print('<?xml version="1.0" encoding="UTF-8"?>')
print('<testsuites>')
print(f'<testsuite name="{sys.argv[2]}" tests="{passes+fails}" failures="{fails}">')
for i in testcases:
    print(i)
print('</testsuite>')
print('</testsuites>')

