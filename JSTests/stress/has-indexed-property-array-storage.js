function shouldBe(actual, expected) {
    if (actual !== expected)
        throw new Error('bad value: ' + actual);
}

function test1(array)
{
    return 2 in array;
}
noInline(test1);

var array = [1, 2, 3, 4];
ensureArrayStorage(array);
for (var i = 0; i < testLoopCount; ++i)
    shouldBe(test1(array), true);

var array = [1, 2, , 4];
ensureArrayStorage(array);
shouldBe(test1(array), false);

var array = [];
ensureArrayStorage(array);
shouldBe(test1(array), false);

function test2(array)
{
    return 2 in array;
}
noInline(test2);

var array1 = [1, 2, 3, 4];
ensureArrayStorage(array1);
var array2 = [1, 2];
ensureArrayStorage(array2);
for (var i = 0; i < testLoopCount; ++i)
    shouldBe(test2(array2), false);
shouldBe(test2(array2), false);
shouldBe(test2(array1), true);
