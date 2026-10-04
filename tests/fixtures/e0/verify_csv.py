import csv,json,sys
from pathlib import Path
rows=list(csv.reader(Path(sys.argv[1]).open(newline="")))
assert rows==[["name","value"],["alpha","1"],["beta","2"]],rows
print(json.dumps({"result":"pass","records":len(rows)}))
