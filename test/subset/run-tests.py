#!/usr/bin/env python3

# Runs a subsetting test suite. Compares the results of subsetting via harfbuzz
# to subsetting via fonttools.

from difflib import unified_diff
import os
import re
import subprocess
import sys
import tempfile
import shutil
import io

from subset_test_suite import SubsetTestSuite

try:
	from fontTools.ttLib import TTFont
except ImportError:
	print ("fonttools is not present, skipping test.")
	sys.exit (77)

ots_sanitize = shutil.which ("ots-sanitize")

def subset_cmd (command):
	global process
	process.stdin.write ((';'.join (command) + '\n').encode ("utf-8"))
	process.stdin.flush ()
	return process.stdout.readline().decode ("utf-8").strip ()

def cmd (command):
	p = subprocess.Popen (
		command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
		universal_newlines=True)
	(stdoutdata, stderrdata) = p.communicate ()
	print (stderrdata, end="", file=sys.stderr)
	return stdoutdata, p.returncode

def fail_test (test, cli_args, message):
	print ('ERROR: %s' % message)
	print ('Test State:')
	print ('  test.font_path    %s' % os.path.abspath (test.font_path))
	print ('  test.profile_path %s' % os.path.abspath (test.profile_path))
	print ('  test.unicodes	    %s' % test.unicodes ())
	expected_file = os.path.join (test_suite.get_output_directory (),
				      test.get_font_ttx_name ())
	print ('  expected_file	    %s' % os.path.abspath (expected_file))
	return 1

def run_test (test, should_check_ots):
	out_file = os.path.join (tempfile.mkdtemp (), test.get_font_name () + '-subset' + test.get_font_extension ())
	cli_args = [hb_subset,
		    "--font-file=" + test.font_path,
		    "--output-file=" + out_file,
		    "--unicodes=%s" % test.unicodes (),
		    "--drop-tables+=DSIG",
		    "--drop-tables-=sbix"]
	cli_args.extend (test.get_profile_flags ())
	print (' '.join (cli_args))
	ret = subset_cmd (cli_args)

	if ret != "success":
		return fail_test (test, cli_args, "%s failed" % ' '.join (cli_args))

	expected_file = os.path.join (test_suite.get_output_directory (), test.get_font_ttx_name ())
	with open(expected_file, encoding="utf-8") as expected_ttx:
		expected_ttx_text = normalize (expected_ttx.read ())

	actual_ttx = io.StringIO ()
	try:
		with TTFont (out_file) as font:
			font.saveXML (actual_ttx, newlinestr="\n")
	except Exception as e:
		print (e)
		return fail_test (test, cli_args, "ttx failed to parse the actual result")

	actual_ttx_text = normalize (actual_ttx.getvalue ())
	actual_ttx.close ()

	if not actual_ttx_text == expected_ttx_text:
		for line in unified_diff (expected_ttx_text.splitlines (1), actual_ttx_text.splitlines (1)):
			sys.stdout.write (line)
		sys.stdout.flush ()
		return fail_test (test, cli_args, 'ttx for expected and actual does not match.')

	if should_check_ots:
		print ("Checking output with ots-sanitize.")
		if not check_ots (out_file):
			return fail_test (test, cli_args, 'ots for subsetted file fails.')

	return 0

CHECKSUMADJUSTMENT_RE = re.compile('checkSumAdjustment value=["]0x([0-9a-fA-F])+["]')
TTLIBVERSION_RE = re.compile(' ttLibVersion=".*"')

def normalize (ttx_string):
	ttx_string = CHECKSUMADJUSTMENT_RE.sub ("", ttx_string, count=1)
	ttx_string = TTLIBVERSION_RE.sub ("", ttx_string, count=1)
	return ttx_string

def has_ots ():
	if not ots_sanitize:
		print ("OTS is not present, skipping all ots checks.")
		return False
	return True

def check_ots (path):
	ots_report, returncode = cmd ([ots_sanitize, path])
	if returncode:
		print ("OTS Failure: %s" % ots_report)
		return False
	return True

args = sys.argv[1:]
if not args or sys.argv[1].find ('hb-subset') == -1 or not os.path.exists (sys.argv[1]):
	sys.exit ("First argument does not seem to point to usable hb-subset.")
hb_subset, args = args[0], args[1:]

if not len (args):
	sys.exit ("No tests supplied.")

has_ots = has_ots()

process = subprocess.Popen ([hb_subset, '--batch'],
                            stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE,
                            stderr=sys.stdout)

fails = 0
for path in args:
	with open (path, mode="r", encoding="utf-8") as f:
		print ("Running tests in " + path)
		test_suite = SubsetTestSuite (path, f.read ())
		for test in test_suite.tests ():
			fails += run_test (test, has_ots)

if fails != 0:
	sys.exit ("%d test(s) failed." % fails)
else:
	print ("All tests passed.")
