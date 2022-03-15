#include "v1model.p4"
struct metadata {
    /* empty */
}

enum bit<7> X {
    Zero = 0,
    One = 1
}

header BITS {
    bit<8> bt;
    int<8> it;
    bool b;
    X  x; 
}

struct headers {
    BITS    bits;
    bit<8>  bt;
    int<8>  it;
    bool    b;
    X       x;
}

parser MyParser(packet_in packet,
                out headers hdr,
                inout metadata meta,
                inout standard_metadata_t standard_metadata) {
    state start {
        packet.extract(hdr.bits);
        hdr.bt = hdr.bits.bt;
        hdr.it = hdr.bits.it;
        hdr.b = hdr.bits.b;
        hdr.x = hdr.bits.x;
        transition accept;
    }
}

control mau(inout headers hdr, inout metadata meta, inout standard_metadata_t sm) {
apply {}
}
control deparse(packet_out pkt, in headers hdr) {
apply {}
}
control verifyChecksum(inout headers hdr, inout metadata meta) {
apply {}
}
control computeChecksum(inout headers hdr, inout metadata meta) {
apply {}
}
V1Switch(MyParser(), verifyChecksum(), mau(), mau(), computeChecksum(),
        deparse()) main;