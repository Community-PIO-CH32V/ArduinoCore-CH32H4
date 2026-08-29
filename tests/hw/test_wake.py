def test_v3f_announces_the_wake(board):
    assert "V3F: waking V5F" in board.banner


def test_v5f_runs_and_identifies_itself(board):
    """NVIC_GetCurrentCoreID returns 1 on the V5F, 0 on the V3F. This is the
    single-ELF design's whole claim: two cores, one link, no merge step."""
    assert "V5F: alive core_id=1" in board.banner


def test_v5f_runs_at_the_full_core_clock(board):
    """The V5F runs at SYSCLK; the V3F at SYSCLK/4. If this core reported
    100 MHz it would be executing the V3F's clock assumptions."""
    assert "V5F: coreclk=400000000" in board.banner


def test_v5f_sees_the_same_bus_clock(board):
    """HCLK is global. Peripheral dividers must come out the same on both
    cores, which is why everything divides ch32h4_hclk()."""
    assert "V5F: hclk=100000000" in board.banner


def test_static_constructors_ran_and_runtime_is_ready(board):
    assert "V5F: runtime ready" in board.banner


def test_the_image_is_a_single_elf(board):
    """No merge step: one .bin covers both cores from 0x08000000."""
    import pathlib
    build = pathlib.Path(__file__).resolve().parents[2] / \
        "tests/sketches/coretest/.pio/build/ch32h417"
    assert len(list(build.glob("*.bin"))) == 1
