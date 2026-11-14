#!/usr/bin/env python3
"""Drum machine editor: patterns, song, kit and sounds, a library, audition through the firmware's own engine, and a USB link to the machine.
   python3 tools/drums/drum_gui.py [--port 5001] [--no-browser]        (pip install -r tools/drums/requirements.txt)"""
import argparse, os, sys, threading, webbrowser
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from drumconf.app import create_app

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", type=int, default=5001); ap.add_argument("--no-browser", action="store_true")
    a = ap.parse_args()
    url = "http://127.0.0.1:%d" % a.port
    if not a.no_browser: threading.Timer(0.8, lambda: webbrowser.open(url)).start()
    print("drum editor on", url, "(Ctrl-C to stop)")
    create_app().run(host="127.0.0.1", port=a.port, debug=False, threaded=True)
