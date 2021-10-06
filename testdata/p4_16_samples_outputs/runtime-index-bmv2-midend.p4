#include <core.p4>
#define V1MODEL_VERSION 20180101
#include <v1model.p4>

typedef bit<48> mac_addr_t;
header aggregator_t {
    bit<8> base0;
    bit<8> base1;
    bit<8> base2;
    bit<8> base3;
    bit<8> val;
}

header vec_e_t {
    bit<8> e;
}

header ml_hdr_t {
    int<8> idx;
}

header ethernet_t {
    mac_addr_t dstAddr;
    mac_addr_t srcAddr;
    bit<16>    etherType;
}

struct headers {
    ethernet_t      ethernet;
    ml_hdr_t        ml;
    vec_e_t[3]      vector;
    aggregator_t[3] pool;
}

struct metadata_t {
    int<8> counter;
}

parser MyParser(packet_in packet, out headers hdr, inout metadata_t meta, inout standard_metadata_t standard_metadata) {
    state start {
        packet.extract<ethernet_t>(hdr.ethernet);
        packet.extract<ml_hdr_t>(hdr.ml);
        packet.extract<vec_e_t>(hdr.vector[0]);
        packet.extract<vec_e_t>(hdr.vector[1]);
        packet.extract<vec_e_t>(hdr.vector[2]);
        packet.extract<aggregator_t>(hdr.pool[0]);
        packet.extract<aggregator_t>(hdr.pool[1]);
        packet.extract<aggregator_t>(hdr.pool[2]);
        meta.counter = 8s0;
        transition accept;
    }
}

control ingress(inout headers hdr, inout metadata_t meta, inout standard_metadata_t standard_metadata) {
    @hidden action runtimeindexbmv2l75() {
        meta.counter = meta.counter + 8s1;
        hdr.vector[0].e = hdr.pool[1].val + 8w1;
        if (hdr.ml.idx == 8s0) {
            hdr.pool[0].val = hdr.pool[1].val + 8w1;
        } else if (hdr.ml.idx == 8s1) {
            hdr.pool[1].val = hdr.pool[1].val + 8w1;
        } else if (hdr.ml.idx == 8s2) {
            hdr.pool[2].val = hdr.pool[1].val + 8w1;
        }
        if (hdr.ml.idx == 8s0) {
            hdr.pool[0].base2 = hdr.vector[0].e;
        } else if (hdr.ml.idx == 8s1) {
            hdr.pool[1].base2 = hdr.vector[0].e;
        } else if (hdr.ml.idx == 8s2) {
            hdr.pool[2].base2 = hdr.vector[0].e;
        }
        if (hdr.ml.idx == 8s0) {
            hdr.vector[1].e = hdr.pool[0].base0;
        } else if (hdr.ml.idx == 8s1) {
            hdr.vector[1].e = hdr.pool[1].base0;
        } else if (hdr.ml.idx == 8s2) {
            hdr.vector[1].e = hdr.pool[2].base0;
        }
        if (hdr.ml.idx == 8s0) {
            hdr.pool[0].base0 = hdr.pool[0].base1 + 8w1;
        } else if (hdr.ml.idx == 8s1) {
            hdr.pool[1].base0 = hdr.pool[1].base1 + 8w1;
        } else if (hdr.ml.idx == 8s2) {
            hdr.pool[2].base0 = hdr.pool[2].base1 + 8w1;
        }
        standard_metadata.egress_spec = standard_metadata.ingress_port;
    }
    @hidden table tbl_runtimeindexbmv2l75 {
        actions = {
            runtimeindexbmv2l75();
        }
        const default_action = runtimeindexbmv2l75();
    }
    apply {
        tbl_runtimeindexbmv2l75.apply();
    }
}

control egress(inout headers hdr, inout metadata_t meta, inout standard_metadata_t standard_metadata) {
    apply {
    }
}

control MyVerifyChecksum(inout headers hdr, inout metadata_t meta) {
    apply {
    }
}

control MyComputeChecksum(inout headers hdr, inout metadata_t meta) {
    apply {
    }
}

control MyDeparser(packet_out packet, in headers hdr) {
    apply {
        packet.emit<ethernet_t>(hdr.ethernet);
        packet.emit<ml_hdr_t>(hdr.ml);
        packet.emit<vec_e_t>(hdr.vector[0]);
        packet.emit<vec_e_t>(hdr.vector[1]);
        packet.emit<vec_e_t>(hdr.vector[2]);
        packet.emit<aggregator_t>(hdr.pool[0]);
        packet.emit<aggregator_t>(hdr.pool[1]);
        packet.emit<aggregator_t>(hdr.pool[2]);
    }
}

V1Switch<headers, metadata_t>(MyParser(), MyVerifyChecksum(), ingress(), egress(), MyComputeChecksum(), MyDeparser()) main;

