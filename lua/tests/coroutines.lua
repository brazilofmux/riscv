-- Basic coroutine
local co = coroutine.create(function()
    print("step 1")
    coroutine.yield()
    print("step 2")
    coroutine.yield()
    print("step 3")
end)
coroutine.resume(co)
coroutine.resume(co)
coroutine.resume(co)

-- Coroutine with values
local co2 = coroutine.create(function()
    local x = coroutine.yield(1)
    local y = coroutine.yield(x + 1)
    return y + 1
end)
local ok, v1 = coroutine.resume(co2)
print(v1)
local ok, v2 = coroutine.resume(co2, 10)
print(v2)
local ok, v3 = coroutine.resume(co2, 20)
print(v3)

-- coroutine.wrap
local gen = coroutine.wrap(function()
    for i = 1, 3 do
        coroutine.yield(i * 10)
    end
end)
print(gen())
print(gen())
print(gen())

-- Status
local co3 = coroutine.create(function() coroutine.yield() end)
print(coroutine.status(co3))
coroutine.resume(co3)
print(coroutine.status(co3))
coroutine.resume(co3)
print(coroutine.status(co3))
