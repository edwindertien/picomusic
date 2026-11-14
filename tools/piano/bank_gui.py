#!/usr/bin/env python3
"""Start the bank configuration GUI:   python3 tools/bank_gui.py [banks/grand.yaml] [--port 5000]"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bankconf.__main__ import main
sys.exit(main(["gui"] + sys.argv[1:]))
