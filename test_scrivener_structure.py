#!/usr/bin/env python3
import os
import xml.etree.ElementTree as ET
import sys

def scan_scrivener(scriv_path):
    scrivx = None
    for f in os.listdir(scriv_path):
        if f.endswith('.scrivx'):
            scrivx = os.path.join(scriv_path, f)
            break
    
    if not scrivx: return

    tree = ET.parse(scrivx)
    root = tree.getroot()
    binder = root.find('Binder')
    if binder is None: return

    def print_item(item, indent=0):
        type = item.get('Type')
        title_elem = item.find('Title')
        title = title_elem.text if title_elem is not None else "Untitled"
        print(f"{'  ' * indent}- [{type}] {title}")
        children = item.find('Children')
        if children is not None:
            for child in children.findall('BinderItem'):
                print_item(child, indent + 1)

    for item in binder.findall('BinderItem'):
        print_item(item)

if __name__ == "__main__":
    scan_scrivener("/home/sheldonl/Dropbox/Apps/Scrivener/Chkmnaar.scriv")
