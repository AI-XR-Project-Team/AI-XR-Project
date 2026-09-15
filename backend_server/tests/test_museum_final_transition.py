"""Static safety checks for the reviewed museum_final destination transition."""
import csv
from pathlib import Path

from scripts.seed_museum_final_destinations import MAP_UUID, PLACEMENT_STATUS, load_plan


ROOT = Path(__file__).resolve().parents[1]
NAV = ROOT / "seeds" / "nav"


def test_museum_final_destination_plan_is_complete_and_is_approach_only():
    plan = load_plan()
    assert str(MAP_UUID) == "5c31f7fc-ef7d-53e7-a4c3-6f5902f4aad9"
    assert [row["display_order"] for row in plan] == [str(index) for index in range(1, 14)]
    assert {row["placement_status"] for row in plan} == {PLACEMENT_STATUS}
    assert [row["node_key"] for row in plan] == [
        "mu-x-amseok", "mu-e-blue-s", "mu-j-go-s", "mu-x-gosaeng",
        "mu-x-jungsaeng", "mu-x-jung-north", "mu-n-jung-s", "mu-n-ante",
        "mu-x-trickart", "mu-x-sinsaeng", "mu-j-sin-door", "mu-e-blue-n",
        "mu-j-amseok",
    ]


def test_museum_final_plan_uses_existing_coordinates_and_one_connected_graph():
    plan = load_plan()
    with (NAV / "nav_nodes.csv").open(encoding="utf-8-sig", newline="") as source:
        nodes = {row["node_key"]: row for row in csv.DictReader(source) if row["map_key"] == "museum_final"}
    with (NAV / "nav_edges.csv").open(encoding="utf-8-sig", newline="") as source:
        edges = [row for row in csv.DictReader(source) if row["map_key"] == "museum_final"]

    targets = {row["node_key"] for row in plan}
    assert targets <= nodes.keys()
    for row in plan:
        node = nodes[row["node_key"]]
        assert (node["pos_x_cm"], node["pos_y_cm"], node["pos_z_cm"]) == (
            row["pos_x_cm"], row["pos_y_cm"], row["pos_z_cm"]
        )

    adjacency = {key: set() for key in nodes}
    for edge in edges:
        adjacency[edge["from_node_key"]].add(edge["to_node_key"])
        if edge["bidirectional"].lower() == "true":
            adjacency[edge["to_node_key"]].add(edge["from_node_key"])
    seen, pending = {next(iter(targets))}, [next(iter(targets))]
    while pending:
        current = pending.pop()
        for next_key in adjacency[current] - seen:
            seen.add(next_key)
            pending.append(next_key)
    assert targets <= seen
