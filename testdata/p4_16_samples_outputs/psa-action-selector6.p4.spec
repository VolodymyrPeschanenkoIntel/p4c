


struct ethernet_t {
	bit<48> dstAddr
	bit<48> srcAddr
	bit<16> etherType
}

struct psa_ingress_output_metadata_t {
	bit<8> class_of_service
	bit<8> clone
	bit<16> clone_session_id
	bit<8> drop
	bit<8> resubmit
	bit<32> multicast_group
	bit<32> egress_port
}

struct psa_egress_output_metadata_t {
	bit<8> clone
	bit<16> clone_session_id
	bit<8> drop
}

struct psa_egress_deparser_input_metadata_t {
	bit<32> egress_port
}

struct a1_1_arg_t {
	bit<48> param
}

struct a1_arg_t {
	bit<48> param
}

struct a2_1_arg_t {
	bit<16> param
}

struct a2_arg_t {
	bit<16> param
}

struct tbl1_set_member_id_arg_t {
	bit<32> member_id
}

struct tbl_set_group_id_arg_t {
	bit<32> group_id
}

struct tbl_set_member_id_arg_t {
	bit<32> member_id
}

struct user_meta_t {
	bit<32> psa_ingress_input_metadata_ingress_port
	bit<8> psa_ingress_output_metadata_drop
	bit<32> psa_ingress_output_metadata_egress_port
	bit<16> local_metadata_data
	bit<32> Ingress_switchExprTmp
	bit<32> Ingress_switchExprTmp_0
	bit<32> Ingress_switchExprTmp_1
	bit<32> Ingress_as_group_id
	bit<32> Ingress_as_member_id
	bit<32> Ingress_ap_member_id
}
metadata instanceof user_meta_t

header ethernet instanceof ethernet_t

action NoAction args none {
	return
}

action a1 args instanceof a1_arg_t {
	mov m.Ingress_switchExprTmp_0 0x0
	mov m.Ingress_switchExprTmp_1 0x0
	mov h.ethernet.dstAddr t.param
	return
}

action a1_1 args instanceof a1_1_arg_t {
	mov m.Ingress_switchExprTmp 0x0
	mov h.ethernet.dstAddr t.param
	return
}

action a2 args instanceof a2_arg_t {
	mov m.Ingress_switchExprTmp_0 0x1
	mov m.Ingress_switchExprTmp_1 0x1
	mov h.ethernet.etherType t.param
	return
}

action a2_1 args instanceof a2_1_arg_t {
	mov m.Ingress_switchExprTmp 0x1
	mov h.ethernet.etherType t.param
	return
}

action tbl_set_group_id args instanceof tbl_set_group_id_arg_t {
	mov m.Ingress_as_group_id t.group_id
	return
}

action tbl_set_member_id args instanceof tbl_set_member_id_arg_t {
	mov m.Ingress_as_member_id t.member_id
	return
}

action tbl1_set_member_id args instanceof tbl1_set_member_id_arg_t {
	mov m.Ingress_ap_member_id t.member_id
	return
}

table tbl {
	key {
		h.ethernet.srcAddr exact
	}
	actions {
		tbl_set_group_id
		tbl_set_member_id
		NoAction
	}
	default_action NoAction args none 
	size 0x10000
}


table as {
	key {
		m.Ingress_as_member_id exact
	}
	actions {
		NoAction
		a1
		a2
	}
	default_action NoAction args none 
	size 0x10000
}


table tbl1 {
	key {
		h.ethernet.srcAddr exact
	}
	actions {
		tbl1_set_member_id
		NoAction
	}
	default_action NoAction args none 
	size 0x10000
}


table ap {
	key {
		m.Ingress_ap_member_id exact
	}
	actions {
		NoAction
		a1_1
		a2_1
	}
	default_action NoAction args none 
	size 0x10000
}


table foo {
	actions {
		NoAction
	}
	default_action NoAction args none 
	size 0x10000
}


table bar {
	actions {
		NoAction
	}
	default_action NoAction args none 
	size 0x10000
}


selector as_sel {
	group_id m.Ingress_as_group_id
	selector {
		m.local_metadata_data
	}
	member_id m.Ingress_as_member_id
	n_groups_max 1024
	n_members_per_group_max 65536
}

apply {
	rx m.psa_ingress_input_metadata_ingress_port
	mov m.psa_ingress_output_metadata_drop 0x1
	extract h.ethernet
	mov m.Ingress_as_member_id 0x0
	mov m.Ingress_as_group_id 0xFFFFFFFF
	mov m.Ingress_switchExprTmp_0 0xFFFFFFFF
	table tbl
	jmpnh LABEL_END
	jmpeq LABEL_END_0 m.Ingress_as_group_id 0xFFFFFFFF
	table as_sel
	LABEL_END_0 :	table as
	LABEL_END :	jmpeq LABEL_SWITCH m.Ingress_switchExprTmp_0 0x0
	jmpeq LABEL_SWITCH_0 m.Ingress_switchExprTmp_0 0x1
	jmp LABEL_ENDSWITCH
	LABEL_SWITCH :	table foo
	jmp LABEL_ENDSWITCH
	LABEL_SWITCH_0 :	table bar
	LABEL_ENDSWITCH :	mov m.Ingress_as_member_id 0x0
	mov m.Ingress_as_group_id 0xFFFFFFFF
	mov m.Ingress_switchExprTmp_1 0xFFFFFFFF
	table tbl
	jmpnh LABEL_END_1
	jmpeq LABEL_END_2 m.Ingress_as_group_id 0xFFFFFFFF
	table as_sel
	LABEL_END_2 :	table as
	LABEL_END_1 :	jmpeq LABEL_SWITCH_1 m.Ingress_switchExprTmp_1 0x0
	jmpeq LABEL_SWITCH_2 m.Ingress_switchExprTmp_1 0x1
	jmp LABEL_ENDSWITCH_0
	LABEL_SWITCH_1 :	table bar
	jmp LABEL_ENDSWITCH_0
	LABEL_SWITCH_2 :	table foo
	LABEL_ENDSWITCH_0 :	mov m.Ingress_ap_member_id 0x0
	mov m.Ingress_switchExprTmp 0xFFFFFFFF
	table tbl1
	jmpnh LABEL_END_3
	table ap
	LABEL_END_3 :	jmpeq LABEL_SWITCH_3 m.Ingress_switchExprTmp 0x0
	jmpeq LABEL_SWITCH_4 m.Ingress_switchExprTmp 0x1
	jmp LABEL_DEFAULT_1
	LABEL_SWITCH_3 :	table bar
	jmp LABEL_ENDSWITCH_1
	LABEL_SWITCH_4 :	table foo
	jmp LABEL_ENDSWITCH_1
	LABEL_DEFAULT_1 :	table foo
	table bar
	LABEL_ENDSWITCH_1 :	jmpneq LABEL_DROP m.psa_ingress_output_metadata_drop 0x0
	emit h.ethernet
	tx m.psa_ingress_output_metadata_egress_port
	LABEL_DROP :	drop
}


