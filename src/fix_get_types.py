#!/usr/bin/env python3

import re
import argparse

parser = argparse.ArgumentParser ()
parser.add_argument ('input')
parser.add_argument ('output')
args = parser.parse_args ()

with open (args.input, 'r') as inp, open (args.output, 'w') as out:
    for l in inp.readlines ():
        l = re.sub ('gobject_hb_', 'hb_gobject_', l)
        l = re.sub ('_t_gobject_get_type', '_get_type', l)
        l = re.sub ('_t_get_gtype', '_get_gtype', l)
        l = re.sub ('_T \(', ' (', l)
        out.write (l)
