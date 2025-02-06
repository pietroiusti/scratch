// map items given op. cf. sicp
function gp_map(items, op) {
    if (items.length===0) {
        return [];
    } else {
        return [op(items[0])].concat(gp_map(items.slice(1), op));
    }
}

gp_map([1,2,3,4,5,6,7,8,9,10], (n)=>n+1);
