import pytest
import math

class MockInstance:
    def __init__(self, obj_id, x=0, y=0):
        self.object_index = obj_id
        self.x = x
        self.y = y
        self.destroyed = False
        self.alarms = {}
        self.vars = {}

    def __getattr__(self, name):
        return self.vars.get(name, 0)

    def __setattr__(self, name, value):
        if name in ('object_index', 'x', 'y', 'destroyed', 'alarms', 'vars'):
            super().__setattr__(name, value)
        else:
            self.vars[name] = value

class GameWorldSimulation:
    """Simulates the GameMaker object lifecycle for Equipment and Artifact of Enigma."""
    # Object IDs from data_mmp.win
    OBJ_EF_MISSILE_BOX = 108
    OBJ_JELLY_MISSILE_FRIENDLY = 247
    OBJ_ENEMY = 321
    OBJ_JELLY_BOX = 463      # Nematocyst Nozzle
    OBJ_MYSTERY = 462        # Artifact of Enigma equipment item

    # The exact 20 items in Enigma choose() pool from gml_Script_use_item bytecode
    ENIGMA_POOL = [
        438, 439, 440, 441, 432, 433, 434, 435, 436, 437,
        419, 423, 424, 425, 426, 427, 428, 429, 430, 431
    ]

    def __init__(self):
        self.instances = []
        self.explosions = []
        self.sounds_played = []

    def instance_create(self, x, y, obj_id):
        inst = MockInstance(obj_id, x, y)
        self.instances.append(inst)
        return inst

    def instance_destroy(self, inst):
        inst.destroyed = True

    def use_item(self, player, item_id, target_item=-1):
        """Simulates gml_Script_use_item."""
        chosen_item = item_id
        if target_item == -1:
            if item_id == 442: # oMystery (Enigma)
                # Chooses from ENIGMA_POOL
                chosen_item = self.ENIGMA_POOL[0]
            else:
                chosen_item = item_id

        if chosen_item == self.OBJ_JELLY_BOX: # 463 Nematocyst Nozzle
            player.item_cooldown = 30 * player.cooldown
            spawner = self.instance_create(player.x, player.y, self.OBJ_EF_MISSILE_BOX)
            spawner.parent = player
            spawner.damage = 2
            spawner.bullets = 6 * (player.clover + 1)
            spawner.jelly = 1
            return spawner
        elif chosen_item in (426, 437, 439):
            # Key, Jar of Souls, Repair Kit
            player.item_cooldown = 45 * player.cooldown
            return None
        return None

    def step_ef_missile_box(self, box):
        """Simulates oEfMissileBox Alarm_0."""
        spawned = []
        for _ in range(box.bullets):
            if box.jelly == 1:
                m = self.instance_create(box.x, box.y, self.OBJ_JELLY_MISSILE_FRIENDLY)
                m.damage = box.damage * 2
                m.parent = box.parent
                m.critical = 0
                m.alarm_0 = 60
                m.direction = 45.0 # initial drift
                m.speed = 5.0
                m.target = None
                m.targetx = -1
                m.targety = -1
                m.homing = 0
                spawned.append(m)
        box.bullets = 0
        self.instance_destroy(box)
        return spawned

    def update_jelly_missile(self, missile, enemies):
        """Simulates oJellyMissileFriendly Alarm_0 and Step_2."""
        # Alarm 0 triggers homing after 60 frames
        if missile.alarm_0 > 0:
            missile.alarm_0 -= 1
            if missile.alarm_0 == 0:
                missile.homing = 1
                if enemies:
                    nearest = min(enemies, key=lambda e: math.hypot(e.x - missile.x, e.y - missile.y))
                    missile.target = nearest
                    missile.targetx = nearest.x
                    missile.targety = nearest.y
                else:
                    missile.target = None
                    # targetx and targety remain -1, -1

                # Reorient direction
                dx = missile.targetx - missile.x
                dy = missile.targety - missile.y
                missile.direction = math.atan2(dy, dx)

        # Step 2 movement
        if missile.homing == 0:
            missile.speed = max(0.0, missile.speed - 0.1)
        else:
            missile.speed = min(10.0, missile.speed + 0.3)

        # Move
        missile.x += missile.speed * math.cos(missile.direction)
        missile.y += missile.speed * math.sin(missile.direction)

        # Check proximity explosion
        dist_to_target = math.hypot(missile.targetx - missile.x, missile.targety - missile.y)
        if dist_to_target < 10.0:
            self.explosions.append((missile.x, missile.y, missile.damage))
            self.instance_destroy(missile)

def test_nematocyst_with_enemies_nearby():
    """Simulates Nematocyst Nozzle with enemy nearby: missiles must home and detonate."""
    sim = GameWorldSimulation()
    player = MockInstance(1, x=200, y=200)
    player.cooldown = 1.0
    player.clover = 0

    enemy = MockInstance(sim.OBJ_ENEMY, x=250, y=200)

    # 1. Player uses Nematocyst Nozzle (ID 463)
    spawner = sim.use_item(player, sim.OBJ_JELLY_BOX)
    assert spawner is not None
    assert spawner.object_index == sim.OBJ_EF_MISSILE_BOX
    assert spawner.jelly == 1
    assert spawner.bullets == 6

    # 2. Spawner creates 6 missiles
    missiles = sim.step_ef_missile_box(spawner)
    assert len(missiles) == 6
    assert all(m.object_index == sim.OBJ_JELLY_MISSILE_FRIENDLY for m in missiles)

    # 3. Simulate frames until homing and impact (drift takes 60 frames, travel takes ~25 frames)
    for _ in range(100):
        for m in missiles:
            if not m.destroyed:
                sim.update_jelly_missile(m, [enemy])

    # All missiles must acquire enemy and detonate near enemy
    assert len(sim.explosions) == 6
    assert all(m.destroyed for m in missiles)

def test_nematocyst_without_enemies_nearby():
    """Simulates Nematocyst Nozzle without enemies: missiles drift, then track to (-1, -1)."""
    sim = GameWorldSimulation()
    player = MockInstance(1, x=500, y=500)
    player.cooldown = 1.0
    player.clover = 0

    spawner = sim.use_item(player, sim.OBJ_JELLY_BOX)
    missiles = sim.step_ef_missile_box(spawner)
    assert len(missiles) == 6

    # Simulate drift phase (60 frames)
    for _ in range(60):
        for m in missiles:
            sim.update_jelly_missile(m, [])

    # Missiles are alive and homing is now enabled
    assert all(not m.destroyed for m in missiles)
    assert all(m.homing == 1 for m in missiles)
    # Target coordinates remain (-1, -1) as per unpatched bytecode
    assert all(m.targetx == -1 and m.targety == -1 for m in missiles)

    # Simulate 50 frames of homing towards (-1, -1)
    initial_x = missiles[0].x
    initial_y = missiles[0].y
    for _ in range(50):
        for m in missiles:
            sim.update_jelly_missile(m, [])

    # Missiles must be moving towards top-left (decreasing x and y towards -1, -1)
    assert missiles[0].x < initial_x
    assert missiles[0].y < initial_y
    # Zero explosions occurred near player
    assert len(sim.explosions) == 0

def test_enigma_item_pool_does_not_contain_nematocyst():
    """Verifies Nematocyst Nozzle (463) is NOT in the 20-item Enigma pool."""
    sim = GameWorldSimulation()
    assert len(sim.ENIGMA_POOL) == 20
    assert sim.OBJ_JELLY_BOX not in sim.ENIGMA_POOL
    # Proof: Nematocyst cannot be rolled by Enigma

def test_enigma_dead_press_items_without_targets():
    """Verifies that contextual items in Enigma produce zero visible effect when conditions are unmet."""
    sim = GameWorldSimulation()
    player = MockInstance(1, x=200, y=200)
    player.cooldown = 1.0

    # Test Key (426) with no chests
    sim.use_item(player, 426)
    assert len(sim.instances) == 0
    assert len(sim.explosions) == 0

    # Test Jar of Souls (437) with no enemies
    sim.use_item(player, 437)
    assert len(sim.instances) == 0
    assert len(sim.explosions) == 0

    # Test Drone Repair Kit (439) with no drones
    sim.use_item(player, 439)
    assert len(sim.instances) == 0
    assert len(sim.explosions) == 0
