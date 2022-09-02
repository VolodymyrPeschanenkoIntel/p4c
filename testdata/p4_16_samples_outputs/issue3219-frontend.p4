control c(out bit<4> result) {
    @name("c.hasReturned") bool hasReturned;
    @name("c.retval") bit<4> retval;
    @name("c.t") bit<4> t_0;
    apply {
        hasReturned = false;
        t_0 = (bit<4>)(int)1;
        hasReturned = true;
        retval = t_0;
        result = retval;
    }
}

control _c(out bit<4> r);
package top(_c _c);
top(c()) main;

