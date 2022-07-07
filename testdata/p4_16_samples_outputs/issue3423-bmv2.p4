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
        transition parse_hdrs;
    }
    state parse_hdrs {
        pkt.extract(hdr.h[0]);
        pkt.extract(hdr.h[1]);
        pkt.extract(hdr.i);
        transition accept;
    }
}

control ingress(inout headers h, inout Meta m, inout standard_metadata_t sm) {
    apply {
        h.i.index = h.h[h.i.index].a;
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
        pkt.emit(h.h[0]);
        pkt.emit(h.h[1]);
        pkt.emit(h.i);
    }
}

V1Switch(p(), vrfy(), ingress(), egress(), update(), deparser()) main;

