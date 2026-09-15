"""Apply the reviewed museum_final destination labels to existing graph nodes only.

Creates the museum map and graph from the existing metric seeds when absent.
Every mapping is an existing approach node from that museum_final graph;
it is not a claim that the coordinate is the specimen's surveyed location.

Run from backend_server after review:
    python -m scripts.seed_museum_final_destinations
"""
import csv
import json
import math
import os
import sys
from collections import deque
from decimal import Decimal

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from app.core.db import SessionLocal  # noqa: E402
from app.models.map_space import MapSpace  # noqa: E402
from app.models.nav_edge import NavEdge  # noqa: E402
from app.models.nav_node import NavNode  # noqa: E402
from scripts.seed_nav import _map_uuid  # noqa: E402

MAP_KEY = "museum_final"
MAP_UUID = _map_uuid(MAP_KEY)
PLACEMENT_STATUS = "existing-gallery-approach"
SEED_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "seeds", "nav")
PLAN_PATH = os.path.join(SEED_DIR, "museum_final_destinations.csv")
REQUIRED_FIELDS = {
    "display_order", "node_key", "pos_x_cm", "pos_y_cm", "pos_z_cm",
    "node_type", "label", "placement_status",
}


def load_plan(path=PLAN_PATH):
    with open(path, encoding="utf-8-sig", newline="") as source:
        reader = csv.DictReader(source)
        if not reader.fieldnames or not REQUIRED_FIELDS.issubset(reader.fieldnames):
            raise ValueError("museum_final destination CSV has missing required columns")
        plan = list(reader)

    if len(plan) != 13:
        raise ValueError(f"museum_final destination CSV must contain 13 rows, got {len(plan)}")
    if [int(row["display_order"]) for row in plan] != list(range(1, 14)):
        raise ValueError("museum_final destination display_order must be 1 through 13")
    if len({row["node_key"] for row in plan}) != len(plan):
        raise ValueError("museum_final destination node_key values must be unique")
    if any(row["placement_status"] != PLACEMENT_STATUS for row in plan):
        raise ValueError(f"museum_final mappings must use {PLACEMENT_STATUS!r} status")
    if any(row["node_type"] not in {"entrance", "exhibit"} for row in plan):
        raise ValueError("museum_final destination node types must be entrance or exhibit")
    return plan


def _coords(row):
    return tuple(Decimal(row[field]) for field in ("pos_x_cm", "pos_y_cm", "pos_z_cm"))


def _require_connected_destinations(db, map_id, nodes):
    ids = {node.id for node in nodes.values()}
    adjacency = {node.id: set() for node in db.query(NavNode).filter_by(map_id=map_id)}
    for edge in db.query(NavEdge).filter_by(map_id=map_id):
        if edge.from_node_id in adjacency:
            adjacency[edge.from_node_id].add(edge.to_node_id)
        if edge.bidirectional and edge.to_node_id in adjacency:
            adjacency[edge.to_node_id].add(edge.from_node_id)

    start = next(iter(ids))
    seen, pending = {start}, deque([start])
    while pending:
        current = pending.popleft()
        for next_id in adjacency.get(current, ()):
            if next_id not in seen:
                seen.add(next_id)
                pending.append(next_id)
    missing = ids - seen
    if missing:
        raise ValueError("museum_final destination nodes are not connected by existing edges")


def read_rows(filename):
    with open(os.path.join(SEED_DIR, filename), encoding="utf-8-sig", newline="") as source:
        return [row for row in csv.DictReader(source) if row.get("map_key") == MAP_KEY]


def ensure_metric_graph(db):
    row = read_rows("map_spaces.csv")[0]
    with open(os.path.join(SEED_DIR, "map_outline.json"), encoding="utf-8-sig") as source:
        outline = next(item for item in json.load(source) if item["map_key"] == MAP_KEY)
    museum = db.query(MapSpace).filter_by(id=MAP_UUID).one_or_none()
    if museum is None:
        museum = MapSpace(id=MAP_UUID)
        db.add(museum)
    museum.name = row["name"]
    museum.origin_note = row["origin_note"]
    museum.coord_system = row["coord_system"]
    museum.outline_json = json.dumps({key: outline[key] for key in ("outline", "obstacles")})
    db.flush()
    nodes = {}
    for row in read_rows("nav_nodes.csv"):
        x, y, z = _coords(row)
        node = db.query(NavNode).filter_by(map_id=MAP_UUID, pos_x_cm=x, pos_y_cm=y, pos_z_cm=z).one_or_none()
        if node is None:
            node = NavNode(map_id=MAP_UUID, pos_x_cm=x, pos_y_cm=y, pos_z_cm=z)
            db.add(node)
        node.node_type, node.label = row["node_type"], row["label"]
        db.flush()
        nodes[row["node_key"]] = node
    for row in read_rows("nav_edges.csv"):
        a, b = nodes[row["from_node_key"]], nodes[row["to_node_key"]]
        edge = db.query(NavEdge).filter_by(map_id=MAP_UUID, from_node_id=a.id, to_node_id=b.id).one_or_none()
        if edge is None:
            edge = NavEdge(map_id=MAP_UUID, from_node_id=a.id, to_node_id=b.id)
            db.add(edge)
        edge.distance_cm = Decimal(row["distance_cm"]) if row["distance_cm"] else Decimal(str(round(math.dist(
            (float(a.pos_x_cm), float(a.pos_y_cm), float(a.pos_z_cm)),
            (float(b.pos_x_cm), float(b.pos_y_cm), float(b.pos_z_cm))), 2)))
        edge.bidirectional = row["bidirectional"].lower() == "true"
        edge.accessible = row["accessible"].lower() == "true"
    db.flush()
    return museum


def apply():
    plan = load_plan()
    db = SessionLocal()
    try:
        museum = ensure_metric_graph(db)

        nodes = {}
        for row in plan:
            x, y, z = _coords(row)
            matches = db.query(NavNode).filter_by(
                map_id=museum.id, pos_x_cm=x, pos_y_cm=y, pos_z_cm=z
            ).all()
            if len(matches) != 1:
                raise ValueError(
                    f"{row['node_key']} must match exactly one existing museum_final node at {x},{y},{z}; "
                    f"found {len(matches)}"
                )
            nodes[row["node_key"]] = matches[0]

        _require_connected_destinations(db, museum.id, nodes)
        for row in plan:
            node = nodes[row["node_key"]]
            node.node_type = row["node_type"]
            node.label = row["label"]

        db.commit()
        print(f"Updated {len(plan)} existing museum_final destination nodes on map {MAP_UUID}.")
    except Exception:
        db.rollback()
        raise
    finally:
        db.close()


if __name__ == "__main__":
    apply()
