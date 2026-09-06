"""The canonical board states. Goldens, demos and the firmware's DEMO_MODE all
draw from this one list, so a state can never be exercised in one and forgotten
in another.

Values are the real ones observed at stop 8213 and Kingsland station on
2026-09-06; see docs/at-api-notes.md.
"""

from .model import Board, Departure, Watch

BUS20 = dict(badge="20", headsign="to Wynyard Quarter", kind="bus")
TRAIN = dict(badge="E-W", headsign="to Britomart", kind="train",
             route_color="97C93D")
BUS22 = dict(badge="22R", headsign="to City Centre", kind="bus")
ONEHUNGA = dict(badge="O-W", headsign="to Onehunga", kind="train",
                route_color="00AEEF")


def _two():
    return [
        Watch(departures=[Departure(240, live=True), Departure(1020)], **BUS20),
        Watch(departures=[Departure(420, live=True), Departure(1320, live=True)], **TRAIN),
    ]


SCENES = {
    "single": Board(
        [Watch(departures=[Departure(240, live=True), Departure(1020)], **BUS20)],
        "17:42"),

    "two_up": Board(_two(), "17:42"),

    "four_up": Board([
        Watch(departures=[Departure(240, live=True), Departure(1020)], **BUS20),
        Watch(departures=[Departure(420, live=True), Departure(1320, live=True)], **TRAIN),
        Watch(departures=[Departure(660, live=True), Departure(1860)], **BUS22),
        Watch(departures=[Departure(1140), Departure(2940)], **ONEHUNGA),
    ], "17:42"),

    # WiFi has been gone for four minutes. Data stays up, marked.
    "stale": Board(_two(), "17:46", stale_s=240),

    # Seven minutes early is real: observed delay was -427s.
    "arriving": Board([
        Watch(departures=[Departure(15, live=True), Departure(1020)], **BUS20),
        Watch(departures=[Departure(420, live=True), Departure(1320, live=True)], **TRAIN),
    ], "17:45"),

    "cancelled": Board([
        Watch(departures=[Departure(300, live=True, cancelled=True),
                          Departure(1200, live=True)], **BUS20),
        Watch(departures=[Departure(420, live=True), Departure(1320, live=True)], **TRAIN),
    ], "17:42"),

    # 1am. Nothing left tonight. This is the state most boards get wrong.
    "empty": Board([
        Watch(departures=[], **BUS20),
        Watch(departures=[], **TRAIN),
    ], "01:12"),

    "dimmed": Board(_two(), "22:30", dimmed=True),
}
