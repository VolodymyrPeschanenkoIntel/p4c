#include <core.p4>
#define V1MODEL_VERSION 20180101
#include <v1model.p4>

header ethernet_t {
    bit<48> dst_addr;
    bit<48> src_addr;
    bit<16> eth_type;
}

header H {
    bit<8> a;
}

struct Headers {
    ethernet_t eth_hdr;
    H[2]       h;
}

struct Meta {
}

parser p(packet_in pkt, out Headers hdr, inout Meta m, inout standard_metadata_t sm) {
    state start {
        pkt.extract<ethernet_t>(hdr.eth_hdr);
        pkt.extract<H>(hdr.h.next);
        pkt.extract<H>(hdr.h.next);
        transition accept;
    }
}

control ingress(inout Headers h, inout Meta m, inout standard_metadata_t sm) {
    @name("ingress.bool_val") bool bool_val_0;
    @name("ingress.tmp_0") bit<3> tmp;
    @name("ingress.tmp_1") bit<3> tmp_0;
    @name("ingress.val_0") bit<3> val;
    @name("ingress.bound_0") bit<3> bound;
    @name("ingress.hasReturned") bool hasReturned;
    @name("ingress.retval") bit<3> retval;
    @name("ingress.tmp") bit<3> tmp_1;
    bit<3> hsiVar;
    @name("ingress.perform_action") action perform_action() {
        val = (bool_val_0 ? 3w0 : val);
        bound = (bool_val_0 ? 3w1 : bound);
        hasReturned = (bool_val_0 ? false : hasReturned);
        tmp_1 = (bool_val_0 ? (val < bound ? val : tmp_1) : tmp_1);
        tmp_1 = (bool_val_0 ? (val < bound ? val : bound) : tmp_1);
        hasReturned = (bool_val_0 ? true : hasReturned);
        retval = (bool_val_0 ? tmp_1 : retval);
        tmp = (bool_val_0 ? retval : tmp);
        tmp_0 = (bool_val_0 ? tmp : tmp_0);
        hsiVar = (bool_val_0 ? tmp_0 : 3w0);
        if (hsiVar == 3w0) {
            h.h[3w0].a = (bool_val_0 ? 8w1 : h.h[3w0].a);
        } else if (hsiVar == 3w1) {
            h.h[3w1].a = (bool_val_0 ? 8w1 : h.h[3w1].a);
        }
    }
    @hidden action predication_issue_3l38() {
        bool_val_0 = true;
    }
    @hidden table tbl_predication_issue_3l38 {
        actions = {
            predication_issue_3l38();
        }
        const default_action = predication_issue_3l38();
    }
    @hidden table tbl_perform_action {
        actions = {
            perform_action();
        }
        const default_action = perform_action();
    }
    apply {
        tbl_perform_action.apply();
    }
}

control vrfy(inout Headers h, inout Meta m) {
    apply {
    }
}

control update(inout Headers h, inout Meta m) {
    apply {
    }
}

control egress(inout Headers h, inout Meta m, inout standard_metadata_t sm) {
    apply {
    }
}

control deparser(packet_out b, in Headers h) {
    apply {
        b.emit<ethernet_t>(h.eth_hdr);
        b.emit<H>(h.h[0]);
        b.emit<H>(h.h[1]);
    }
}

V1Switch<Headers, Meta>(p(), vrfy(), ingress(), egress(), update(), deparser()) main;

