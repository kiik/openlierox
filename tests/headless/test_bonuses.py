"""Headless bonus test: a client joining a running game sees the bonuses on it.

Bonuses are announced only at the moment they spawn, to whoever is connected
then, so a client that joins later never learns about the bonuses already lying
on the map: they stay invisible to that client until they expire, while
everyone else can still collect them.

The set-up keeps the bonuses fixed rather than letting the engine keep spawning
them. The periodic spawner and the expiry check are both gated on the Bonuses
setting, which stays off; the server places its bonuses through the
``spawnBonus`` test hook instead, while the round is running and *before* the
late joiner connects. So nothing spawns after the join, and any bonus the
joiner reports can only have reached it as part of joining -- if new ones kept
appearing on a timer, the joiner would receive the next one within a second and
the test would pass whether or not joining replays anything.

The test currently fails, and the cause is not the missing replay alone.
Measuring the channel while a client joins a running game shows that reliable
delivery to that client wedges: its ``CChannel3`` queue accepts the packets, but
``ReliableOut`` saturates at ``MaxNonAcknowledgedPackets`` (3) and never drains
again, so the queue sits at a non-zero depth for the rest of the round. Nothing
sent to a mid-game joiner over the reliable channel arrives -- not the bonuses,
and not the rest of the join burst either (the joiner never dispatches
``S2C_WORMINFO``, ``S2C_CLREADY``, ``S2C_TEAMSCOREUPDATE``, ``S2C_FLAGINFO`` or
``S2C_SETWORMPROPS``). What state it does get arrives over the attribute-sync
path, ``S2C_GAMEATTRUPDATE``.

So this is kept as an expected failure: it documents the bonus symptom and gives
the reproduction, but the fix belongs with the channel, not here. Remove the
marker once reliable delivery to a mid-game joiner works.
"""

import pytest

BONUSES = 4


@pytest.mark.xfail(reason="reliable delivery to a mid-game joiner wedges; see module docstring")
def test_late_joiner_receives_bonuses_already_on_the_map(network_game):
    # Bots get the round running with no client in it,
    # so the bonuses are all placed before the joiner exists.
    # Two of them rather than one,
    # so a death match cannot reach game over for lack of an opponent
    # and drop back to the lobby before the joiner arrives.
    server = network_game.start_server(
        OLX_BOTS=2,
        OLX_START_WHEN_WORMS=2,
        OLX_SPAWN_BONUSES=BONUSES,
        OLX_RUN_SECONDS=120,
    )
    assert server.wait_for("SERVER_LOBBY", timeout=30), server.read_log()
    assert server.wait_for("SERVER_PLAYING", timeout=45), (
        "server never started the round:\n" + server.read_log())
    assert server.wait_for("SERVER_BONUSES n=%d" % BONUSES, timeout=30), (
        "server never had %d bonuses on the map:\n%s"
        % (BONUSES, server.read_log()))

    late = network_game.add_client(
        "late", env={"OLX_EMIT_BONUSES": "1", "OLX_RUN_SECONDS": "90"})
    assert late.wait_for("CLIENT[late] PLAYING", timeout=45), (
        "late joiner never reached the running game:\n" + late.read_log())

    # The joiner must end up with the same bonuses the server has.
    # Without the replay it reports n=0 for the whole round.
    assert late.wait_for("CLIENT[late] BONUSES n=%d" % BONUSES, timeout=30), (
        "late joiner never received the %d bonuses already on the map:\n%s"
        % (BONUSES, late.read_log()))
