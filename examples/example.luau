-- example.luau

-- Handles are opaque values owned by the server; just pass them back to the API.
type Handle = any
type Vec3 = { x: number, y: number, z: number }
type Block = { id: number, meta: number }
type ItemStack = { id: number, count: number, data: number }

log.info("Loaded!")

function OnPlayerJoin(playerHandle: Handle)
    player.sendMessage(playerHandle, "Welcome to the server!")
end

function OnPlayerLeave(playerHandle: Handle)
    log.info(`{player.getUsername(playerHandle)} left the server`)
end

function OnPlayerChat(playerHandle: Handle, message: string): boolean?
    log.info(`{player.getUsername(playerHandle)} said: {message}`)

    -- Returning false cancels the chat message.
    if message == "banned word" then
        player.sendMessage(playerHandle, "Watch your language!")
        return false
    end

    if message == "kick me" then
        player.kick(playerHandle, "Asked for it")
    end

    return nil
end

function OnPlayerMove(playerHandle: Handle, from: Vec3, to: Vec3): boolean?
    -- Simple example: stop anyone from going below bedrock.
    if to.y < 0 then
        return false
    end

    return nil
end

function OnItemUse(playerHandle: Handle, itemStack: ItemStack)
    log.info(`{player.getUsername(playerHandle)} used item id={itemStack.id}`)
end

function OnBlockUse(playerHandle: Handle, worldHandle: Handle, x: number, y: number, z: number)
    local block: Block = world.getBlock(worldHandle, x, y, z)
    log.info(string.format("Block used at (%d, %d, %d): id=%d meta=%d", x, y, z, block.id, block.meta))

    -- Remember the last block each player used, using the data.* API.
    data.setPlayer(playerHandle, { lastBlockId = block.id, x = x, y = y, z = z })
end

function OnBlockBreak(playerHandle: Handle, worldHandle: Handle, tool: ItemStack, x: number, y: number, z: number, block: Block)
    log.info(string.format("%s broke block id=%d at (%d, %d, %d) with item id=%d",
        player.getUsername(playerHandle), block.id, x, y, z, tool.id))
end

function OnBlockPlace(playerHandle: Handle, worldHandle: Handle, x: number, y: number, z: number, blockId: number): boolean?
    log.info(string.format("%s placed block id=%d at (%d, %d, %d)",
        player.getUsername(playerHandle), blockId, x, y, z))

    -- Protect id 7 (e.g. bedrock) from being placed by players.
    if blockId == 7 then
        return false
    end

    return nil
end

function OnEntityDamage(entityHandle: Handle, amount: number)
    local pos: Vec3 = entity.getPosition(entityHandle)
    log.info(string.format("Entity at (%.1f, %.1f, %.1f) took %d damage", pos.x, pos.y, pos.z, amount))
end

-- Careful with this one: it fires every server tick, so keep it cheap.
local tickCount = 0
function OnServerTick()
    tickCount += 1
end

function OnUnload()
    log.info(`Unloading, bye! Saw {tickCount} ticks.`)
end
