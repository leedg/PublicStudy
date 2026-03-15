import re, os, xml.etree.ElementTree as ET

SRC = "E:/MyGitHub/PublicStudy/NetworkModuleTest/Doc/Reports/Network_Async_DB_Report_img"
DST = "E:/MyGitHub/PublicStudy/NetworkModuleTest/Doc/Reports/Network_Async_DB_Report_img_C"

os.makedirs(DST, exist_ok=True)

def read_file(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()

def write_file(path, content):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)

def apply_font_size_upgrades(text):
    text = re.sub(r"fontSize=8\b", "fontSize=11", text)
    text = re.sub(r"fontSize=9\b", "fontSize=12", text)
    return text

def update_page_dims(text, new_width=None, new_height=None):
    if new_width:
        text = re.sub(r'pageWidth="\d+"', 'pageWidth="' + str(new_width) + '"', text)
    if new_height:
        text = re.sub(r'pageHeight="\d+"', 'pageHeight="' + str(new_height) + '"', text)
    return text

def insert_before_end_root(text, new_cells):
    return text.replace("</root>", new_cells + "\n  </root>", 1)

print("helper functions defined ok")
