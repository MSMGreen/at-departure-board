import pytest

from tools.board import layout, sprites


# --- lane division -------------------------------------------------------

@pytest.mark.parametrize("n,expected", [(1, 222), (2, 111), (3, 74), (4, 55)])
def test_lane_height_divides_the_area_below_the_status_bar(n, expected):
    assert layout.lane_rects(n)[0].height == expected


@pytest.mark.parametrize("n", [1, 2, 3, 4])
def test_lanes_start_below_the_status_bar(n):
    assert layout.lane_rects(n)[0].y0 == layout.STATUS_H


@pytest.mark.parametrize("n", [1, 2, 3, 4])
def test_lanes_never_overflow_the_screen(n):
    assert layout.lane_rects(n)[-1].y1 <= layout.H


@pytest.mark.parametrize("n", [2, 3, 4])
def test_lanes_do_not_overlap(n):
    rects = layout.lane_rects(n)
    for a, b in zip(rects, rects[1:]):
        assert a.y1 <= b.y0


@pytest.mark.parametrize("n", [0, 5])
def test_lane_count_outside_one_to_four_is_rejected(n):
    with pytest.raises(ValueError):
        layout.lane_rects(n)


# --- vehicle position: the whole point of the design ---------------------

def test_at_the_horizon_the_vehicle_is_at_the_far_left():
    ln = layout.lane(0, 2)
    assert layout.vehicle_x(layout.HORIZON_S, ln, "bus") == ln.track.x0


def test_at_zero_the_vehicle_has_arrived_at_the_marker():
    ln = layout.lane(0, 2)
    expected = ln.marker_x - sprites.SPRITES["bus"].width
    assert layout.vehicle_x(0, ln, "bus") == expected


def test_beyond_the_horizon_clamps_to_the_far_left():
    ln = layout.lane(0, 2)
    assert layout.vehicle_x(99999, ln, "bus") == ln.track.x0


def test_after_departure_clamps_to_the_marker():
    ln = layout.lane(0, 2)
    assert layout.vehicle_x(-600, ln, "bus") == layout.vehicle_x(0, ln, "bus")


def test_half_the_horizon_is_about_half_the_track():
    ln = layout.lane(0, 2)
    lo = layout.vehicle_x(layout.HORIZON_S, ln, "bus")
    hi = layout.vehicle_x(0, ln, "bus")
    mid = layout.vehicle_x(layout.HORIZON_S // 2, ln, "bus")
    assert abs(mid - (lo + hi) // 2) <= 1


def test_position_advances_monotonically_as_time_runs_down():
    ln = layout.lane(0, 3)
    xs = [layout.vehicle_x(s, ln, "train") for s in range(1200, -1, -30)]
    assert xs == sorted(xs)


def test_a_train_is_wider_so_it_stops_further_left_than_a_bus():
    ln = layout.lane(0, 2)
    assert layout.vehicle_x(0, ln, "train") < layout.vehicle_x(0, ln, "bus")


# --- lane internals ------------------------------------------------------

@pytest.mark.parametrize("n", [1, 2, 3, 4])
def test_sprite_baseline_sits_on_the_track(n):
    ln = layout.lane(0, n)
    assert ln.sprite_baseline == ln.track.y0


@pytest.mark.parametrize("n", [1, 2, 3, 4])
def test_track_stops_short_of_the_marker(n):
    ln = layout.lane(0, n)
    assert ln.track.x1 <= ln.marker_x


@pytest.mark.parametrize("n", [1, 2, 3, 4])
def test_everything_stays_inside_its_lane(n):
    ln = layout.lane(0, n)
    for name, xy in [("headsign", ln.headsign_xy), ("minutes", ln.minutes_xy),
                     ("following", ln.following_xy)]:
        assert ln.rect.y0 <= xy[1] <= ln.rect.y1, f"{name} escaped lane"


@pytest.mark.parametrize("n", [1, 2, 3, 4])
def test_the_widest_sprite_always_fits_the_track(n):
    ln = layout.lane(0, n)
    assert ln.track.width >= sprites.SPRITES["train"].width


def test_lane_index_selects_the_right_band():
    assert layout.lane(2, 4).rect.y0 == layout.lane_rects(4)[2].y0
