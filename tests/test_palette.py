from tools.board import palette


def test_leading_hash_is_accepted():
    assert palette.parse_hex("#97C93D") == (151, 201, 61)


def test_black_is_treated_as_absent():
    assert palette.parse_hex("000000") is None


def test_malformed_is_none():
    assert palette.parse_hex("nonsense") is None


def test_bright_moves_away_from_black():
    assert palette.bright((10, 10, 10)) > (10, 10, 10)
