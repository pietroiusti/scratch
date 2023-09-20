// Print the path of those properties in obj whose value is equal to something.
//
// Avoid circular references.

var obj = {
    foo: "foobazza",
    bar: {
        fo: 'fofo',
    },
    baz: {
        barbaz: {
            fooz: "foobazza",
            bafoo: {
                bazbar: "foobazza",
            },
        },
    },
};

function printProp(obj, val) {
    const cache = new Set();

    function printPropRec(obj, val, initialStr) {
        Object.keys(obj).forEach(key => {
            if(!obj[key]) return;

            if (! cache.has(obj[key])) {
                if (typeof obj[key] === 'object') {
                    cache.add(obj[key]);
                    printPropRec(obj[key], val, initialStr+'.'+key);
                } else {
                    if (obj[key] == val) {
                        console.log('obj'+initialStr+'.'+key);
                    }
                }
            }

        });
    }

    printPropRec(obj, val, "");
}

printProp(obj, "foobazza");
