# -*- coding: utf-8 -*-
import re, os, xml.etree.ElementTree as ET

SRC = 'E:/MyGitHub/PublicStudy/NetworkModuleTest/Doc/Reports/Network_Async_DB_Report_img'
DST = 'E:/MyGitHub/PublicStudy/NetworkModuleTest/Doc/Reports/Network_Async_DB_Report_img_C'
os.makedirs(DST, exist_ok=True)

def rd(p):
    with open(p, 'r', encoding='utf-8') as f: return f.read()

def wr(p, c):
    with open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(c)

def upfonts(t):
    t = re.sub(r'fontSize=8(?=[^0-9])', 'fontSize=11', t)
    t = re.sub(r'fontSize=9(?=[^0-9])', 'fontSize=12', t)
    return t

def updims(t, w=None, h=None):
    if w: t = re.sub(r'pageWidth="\d+"', 'pageWidth="'+str(w)+'"', t)
    if h: t = re.sub(r'pageHeight="\d+"', 'pageHeight="'+str(h)+'"', t)
    return t

def inj(t, cells):
    return t.replace('</root>', cells + chr(10) + '  </root>', 1)

print('helpers ready')