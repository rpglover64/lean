local s = sexpr(Local("a", Bool), Local("b", Bool))
print(s)
local a, b = s:fields()
print(a)
print(b)
assert(a ~= Local("a", Bool))
assert(a:to_external() == Local("a", Bool))
assert(a:fields() == Local("a", Bool))
assert(is_expr(a:to_external()))

local s = sexpr(Local("a", Bool), Local("b", Bool))
local s = sexpr({})

local s1 = sexpr(Local("a", Bool), Local("b", Bool))
local s2 = sexpr(Local("a", Bool), Local("c", Bool))
assert(Local("b", Bool) > Local("c", Bool))
assert(s1 > s2)
assert(s2 < s1)
assert(s2 == sexpr(Local("a", Bool), Local("c", Bool)))
