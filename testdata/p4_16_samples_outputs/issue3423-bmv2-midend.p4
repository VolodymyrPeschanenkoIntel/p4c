#include <core.p4>
#define V1MODEL_VERSION 20180101
#include <v1model.p4>

header h_stack {
    bit<32> a;
}

header h_index {
    bit<32> index;
}

struct headers {
    h_stack[2] h;
    h_index    i;
}

struct Meta {
}

parser p(packet_in pkt, out headers hdr, inout Meta m, inout standard_metadata_t sm) {
    state start {
        pkt.extract<h_stack>(hdr.h[0]);
        pkt.extract<h_stack>(hdr.h[1]);
        pkt.extract<h_index>(hdr.i);
        transition accept;
    }
}

control ingress(inout headers h, inout Meta m, inout standard_metadata_t sm) {
    bit<32> hsiVar;
    bit<32> hsVar;
    @hidden action issue3423bmv2l35() {
        h.i.index = h.h[32w0].a;
    }
    @hidden action issue3423bmv2l35_0() {
        h.i.index = h.h[32w1].a;
    }
    @hidden action issue3423bmv2l35_1() {
        h.i.index = hsVar;
    }
    @hidden action issue3423bmv2l35_2() {
        hsiVar = h.i.index;
    }
    @hidden table tbl_issue3423bmv2l35 {
        actions = {
            issue3423bmv2l35_2();
        }
        const default_action = issue3423bmv2l35_2();
    }
    @hidden table tbl_issue3423bmv2l35_0 {
        actions = {
            issue3423bmv2l35();
        }
        const default_action = issue3423bmv2l35();
    }
    @hidden table tbl_issue3423bmv2l35_1 {
        actions = {
            issue3423bmv2l35_0();
        }
        const default_action = issue3423bmv2l35_0();
    }
    @hidden table tbl_issue3423bmv2l35_2 {
        actions = {
            issue3423bmv2l35_1();
        }
        const default_action = issue3423bmv2l35_1();
    }
    apply {
        tbl_issue3423bmv2l35.apply();
        if (hsiVar == 32w0) {
            tbl_issue3423bmv2l35_0.apply();
        } else if (hsiVar == 32w1) {
            tbl_issue3423bmv2l35_1.apply();
        } else if (hsiVar >= 32w1) {
            tbl_issue3423bmv2l35_2.apply();
        }
    }
}

control vrfy(inout headers h, inout Meta m) {
    apply {
    }
}

control update(inout headers h, inout Meta m) {
    apply {
    }
}

control egress(inout headers h, inout Meta m, inout standard_metadata_t sm) {
    apply {
    }
}

control deparser(packet_out pkt, in headers h) {
    apply {
        pkt.emit<h_stack>(h.h[0]);
        pkt.emit<h_stack>(h.h[1]);
        pkt.emit<h_index>(h.i);
    }
}

V1Switch<headers, Meta>(p(), vrfy(), ingress(), egress(), update(), deparser()) main;

