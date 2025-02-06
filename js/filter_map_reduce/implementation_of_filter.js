// filter items given pred. cf. sicp
function gp_filter(items, pred) {
    if (items.length===0) {
        return [];
    } else {
        if (pred(items[0])) {
            return [items[0]].concat(gp_filter(items.slice(1), pred));
        } else {
            return gp_filter(items.slice(1), pred);
        }
    }
}

console.log(gp_filter([1,2,3,4,5,6,7,8,9,10], (n)=>n%2===0));
