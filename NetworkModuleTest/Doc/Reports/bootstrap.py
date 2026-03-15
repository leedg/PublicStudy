import base64, os
code = base64.b64decode(b\042PLACEHOLDER\042).decode(\042utf-8\042)
with open(\042E:/MyGitHub/PublicStudy/NetworkModuleTest/Doc/Reports/gen_c_full.py\042, \042w\042, encoding=\042utf-8\042) as f: f.write(code)
print(\042gen_c_full.py written\042)