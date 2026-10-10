import transit_router

def test_add_seconds():
    assert transit_router.add_seconds(30, 12) == 42
