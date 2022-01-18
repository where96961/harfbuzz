#!/usr/bin/env python3

"This tool is intended to be used from meson"

import os, os.path, sys, subprocess, shutil

uninstalled_error = """
'ragel' is missing on your system. In order to develop HarfBuzz itself,
specifically, by editing the *.rl source files, you have to install ragel
in order to transpile them to C++ headers. Try configuring meson with
`--wrap-mode=default -Dragel=enabled`
""".strip()

ragel = sys.argv[1]

if len (sys.argv) != 5:
	sys.exit (__doc__)

hh = sys.argv[2]
CURRENT_SOURCE_DIR = sys.argv[3]
rl = sys.argv[4]

outdir = os.path.dirname (hh)
hh_in_src = os.path.join (CURRENT_SOURCE_DIR, os.path.basename (hh))

if os.stat (rl).st_mtime <= os.stat (hh_in_src).st_mtime:
	shutil.copy (hh_in_src, hh)
else:
	if ragel == 'error' or not os.path.exists (ragel):
		sys.exit (uninstalled_error)
	subprocess.check_call ([ragel, '-e', '-F1', '-o', hh, rl])
	# copy it also to src/
	shutil.copyfile (hh, hh_in_src)
