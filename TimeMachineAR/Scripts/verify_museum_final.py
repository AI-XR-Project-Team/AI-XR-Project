"""Read-only live API verification of the reviewed museum destination graph."""
import json
from urllib.request import Request, urlopen

BASE = "http://192.168.219.100:8000"
MAP_ID = "5c31f7fc-ef7d-53e7-a4c3-6f5902f4aad9"
EXPECTED = ["자수정", "입구", "삼엽충", "고사리잎", "아르켈론", "알로사우루스", "화석",
            "물고기화석", "진주화석", "포유류 화석", "거북이 화석", "출구", "탄생석"]


def request(path, body=None):
    data = None if body is None else json.dumps(body).encode()
    with urlopen(Request(BASE + path, data=data, headers={"Content-Type": "application/json"}), timeout=15) as response:
        return json.load(response)


graph = request(f"/maps/{MAP_ID}/graph")
destinations = {n["label"]: n for n in graph["nodes"] if n["node_type"] in ("exhibit", "entrance", "facility", "exit")}
assert set(destinations) == set(EXPECTED), destinations.keys()
start = destinations["입구"]
for label in EXPECTED:
    if label == "입구":
        continue
    route = request("/navigation/route", {
        "map_id": MAP_ID,
        "from": {k: start[k] for k in ("pos_x_cm", "pos_y_cm", "pos_z_cm")},
        "to": {"node_id": destinations[label]["node_id"]},
        "accessible_only": False,
    })
    assert route.get("waypoints"), (label, route)
print(json.dumps({"map_id": MAP_ID, "nodes": len(graph["nodes"]), "edges": len(graph["edges"]),
                  "destinations": len(destinations), "entrance_routes_passed": 12}, indent=2))
