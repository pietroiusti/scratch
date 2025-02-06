// accumulate (fold-right). cf. sicp
function gp_accumulate(items, init, op) {
    if (items.length===0) {
        return init;
    } else {
        return op(items[0], gp_accumulate(items.slice(1), init, op));
    }
}

// divide numbers into even and odd
gp_accumulate([1,2,3,4,5,6,7,8,9,10],
              [[],[]],
              (i,acc) => {
                  if (i%2===0) {
                      acc[0].push(i);
                      return acc;
                  } else {
                      acc[1].push(i);
                      return acc;
                  }
              });
