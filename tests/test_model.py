import pytest

from tools.board.model import Board, Departure, Watch


def w(**kw):
    base = dict(badge="20", headsign="to Wynyard Quarter", kind="bus",
                departures=[Departure(240), Departure(1020)])
    base.update(kw)
    return Watch(**base)


def test_next_and_following_are_the_first_two():
    x = w()
    assert x.next.eta_s == 240
    assert x.following.eta_s == 1020


def test_next_is_none_when_nothing_is_scheduled():
    x = w(departures=[])
    assert x.next is None
    assert x.following is None


def test_following_is_none_when_only_one_departure():
    x = w(departures=[Departure(240)])
    assert x.next.eta_s == 240
    assert x.following is None


def test_departures_are_ordered_by_eta_regardless_of_input_order():
    x = w(departures=[Departure(900), Departure(120), Departure(400)])
    assert [d.eta_s for d in x.departures] == [120, 400, 900]


def test_departed_is_negative_eta():
    assert Departure(-5).is_departed
    assert not Departure(0).is_departed


def test_board_is_stale_only_past_the_threshold():
    assert not Board([w()], "17:42", stale_s=0).is_stale
    assert not Board([w()], "17:42", stale_s=89).is_stale
    assert Board([w()], "17:42", stale_s=91).is_stale


def test_board_rejects_more_than_four_watches():
    with pytest.raises(ValueError):
        Board([w()] * 5, "17:42")


def test_board_rejects_zero_watches():
    with pytest.raises(ValueError):
        Board([], "17:42")


def test_a_watch_has_no_message_by_default():
    assert Watch(badge="20", headsign="x", kind="bus").message is None


def test_a_board_defaults_to_the_kingsland_location():
    assert Board([Watch(badge="20", headsign="x", kind="bus")], "17:42").location == "Kingsland"
