#!/usr/bin/python3
import sys
import struct
import wrapper
import threading
import time
import os
from wrapper import recv_from_any_link, send_to_link, get_switch_mac, get_interface_name

CAM_table = {}
# tabela CAM care va retine mapare intre adresa MAC si interfata
interfaces_vlan_t = {}
# dictionar care va retine tipul interfetei(T sau numarul VLAN-ului)
state = {}
# dictionar care va retine starea porturilor(0 sau 1 / blocked sau listening)
interfaces_aux = []
# lista auxiliara care va retine interfetele pentru functiile care nu primesc implicit argumente

priority, root_port, own_bridge_id, root_bridge_id, root_path_cost = 0, 0, 0, 0, 0

def parse_ethernet_header(data):
    # Unpack the header fields from the byte array
    #dest_mac, src_mac, ethertype = struct.unpack('!6s6sH', data[:14])
    dest_mac = data[0:6]
    src_mac = data[6:12]
    
    # Extract ethertype. Under 802.1Q, this may be the bytes from the VLAN TAG
    ether_type = (data[12] << 8) + data[13]

    vlan_id = -1
    # Check for VLAN tag (0x8100 in network byte order is b'\x81\x00')
    if ether_type == 0x8200:
        vlan_tci = int.from_bytes(data[14:16], 'big')
        vlan_id = vlan_tci & 0x0FFF  # extract the 12-bit VLAN ID
        ether_type = (data[16] << 8) + data[17]

    return dest_mac, src_mac, ether_type, vlan_id

def create_vlan_tag(vlan_id):
    # 0x8100 for the Ethertype for 802.1Q
    # vlan_id & 0x0FFF ensures that only the last 12 bits are used
    return struct.pack('!H', 0x8200) + struct.pack('!H', vlan_id & 0x0FFF)

def check_bpdu_frame(data, interface):
    #functie care face forward la un cadru BPDU ,respectand pseudocodul oferit 
    global state, interfaces_aux, root_port, own_bridge_id, root_bridge_id, root_path_cost
    bpdu_root_bridge_id = int.from_bytes(data[22:30], 'big')
    sender_path_cost = int.from_bytes(data[30:34], 'big')
    sender_bridge_id = int.from_bytes(data[34:42], 'big')
    
    if bpdu_root_bridge_id < root_bridge_id:
        root_path_cost = sender_path_cost + 10
        root_port = interface
        if root_bridge_id == own_bridge_id:
            for i in interfaces_aux:
                if i != root_port:
                    state[get_interface_name(i)] = 0
        root_bridge_id = bpdu_root_bridge_id
        if state[get_interface_name(root_port)] == 0:
            state[get_interface_name(root_port)] = 1
        for i in interfaces_aux:
            if interfaces_vlan_t[get_interface_name(i)] == 'T':
                send_to_link(i, 52, data[0:30] + root_path_cost.to_bytes(4, 'big') + own_bridge_id.to_bytes(8, 'big') + data[42:])
    elif bpdu_root_bridge_id == root_bridge_id:
        if interface == root_port and sender_path_cost + 10 < root_path_cost:
            root_path_cost = sender_path_cost + 10
        elif interface != root_port:
            if sender_path_cost > root_path_cost and state[get_interface_name(interface)] != 1:
                state[get_interface_name(interface)] = 1
    elif sender_bridge_id == own_bridge_id:
        state[get_interface_name(interface)] = 0
    else:
        return
    
    if own_bridge_id == root_bridge_id:
        for i in interfaces_aux:
            if interfaces_vlan_t[get_interface_name(i)] == 'T':
                state[get_interface_name(i)] = 1

def send_bpdu_every_sec():
    #functie care trimite un BPDU la fiecare secunda, daca switch-ul este root bridge
    global own_bridge_id, root_bridge_id, interfaces_aux
    while True:
        time.sleep(1)
        if own_bridge_id == root_bridge_id:
            for i in interfaces_aux:
                if interfaces_vlan_t[get_interface_name(i)] == 'T':
                    dest_mac = struct.pack('!6B', 0x01, 0x80, 0xc2, 0x00, 0x00, 0x00)
                    #setam adresa MAC destinatie data in enunt
                    src_mac = get_switch_mac()
                    data = bytearray()
                    llc_length = 52
                    llc_length_bytes = llc_length.to_bytes(2, 'big')
                    dsap = b'\x42'
                    ssap = b'\x42'
                    control = b'\x03'
                    llc_header = dsap + ssap + control
                    flags = b'\x00'
                    bpdu_header = b'\x00\x00\x00\x00'
                    bpdu_config = bytearray()
                    bpdu_config.extend(flags)
                    bpdu_config.extend(int(root_bridge_id).to_bytes(8, 'big'))
                    bpdu_config.extend(int(root_path_cost).to_bytes(4, 'big'))
                    bpdu_config.extend(int(own_bridge_id).to_bytes(8, 'big'))
                    bpdu_config.extend(int(i).to_bytes(2, 'big'))
                    message_age = b'\x00\x01'
                    max_age = b'\x00\x14'
                    hello_time = b'\x00\x02'
                    forward_delay = b'\x00\x0F'
                    bpdu_config.extend(message_age + max_age + hello_time + forward_delay)
                    data = dest_mac + src_mac + llc_length_bytes + llc_header + bpdu_header + bpdu_config
                    # construim BPDU-ul conform capturii de trafic si a structurii oferite
                    send_to_link(i, 52, data)

def send_unicast(interface, out_interface, vlan_id, data, length):
    # functie care trimite un cadru unicast considerand interfata de pe care a venit si cea pe care trebuie sa o trimita
    global interfaces_vlan_t, state
    if interfaces_vlan_t[get_interface_name(out_interface)] != 'T':
        # daca interfata de iesire este de tip access
        if interfaces_vlan_t[get_interface_name(interface)] == 'T' and state[get_interface_name(interface)] == 1:
            # daca interfata de intrare este de tip trunk si este in starea listening
            if interfaces_vlan_t[get_interface_name(out_interface)] == vlan_id:
                data = data[0:12] + data[16:]
                send_to_link(out_interface, length - 4, data)
                # eliminam tag-ul VLAN si trimitem cadrul
        elif interfaces_vlan_t[get_interface_name(out_interface)] == interfaces_vlan_t[get_interface_name(interface)]:
            send_to_link(out_interface, length, data)
            # daca interfata de intrare este de tip access si este in acelasi VLAN cu cea de intrare, trimitem cadrul
    elif state[get_interface_name(out_interface)] == 1:
        # daca interfata de iesire este de tip trunk si este in starea listening
        if interfaces_vlan_t[get_interface_name(interface)] != 'T':
            data = data[0:12] + create_vlan_tag(interfaces_vlan_t[get_interface_name(interface)]) + data[12:]
            send_to_link(out_interface, length + 4, data)
            # daca interfata de intrare este de tip access, adaugam tag-ul VLAN si trimitem cadrul
        else:
            send_to_link(out_interface, length, data)
            # daca interfata de intrare este de tip trunk, trimitem cadrul

def send_broadcast(interfaces_aux, interface, data, length, vlan_id):
    for i in interfaces_aux:
        if i != interface:
            send_unicast(interface, i, vlan_id, data, length)
    # facem broadcast pe toate interfetele in afara de cea pe care am primit cadrul
            
def frame_logic(interface, data, length ,dest_mac, vlan_id):
    if dest_mac == '01:80:c2:00:00:00':
        #adresa de multicast
        check_bpdu_frame(data, interface)
    else:
        if dest_mac != 'ff.ff.ff.ff.ff.ff':
            if dest_mac in CAM_table:
                # daca e unicast
                send_unicast(interface, CAM_table[dest_mac], vlan_id, data, length)
            else:
                send_broadcast(interfaces_aux, interface, data, length, vlan_id)
        else:
            send_broadcast(interfaces_aux, interface, data, length, vlan_id)

def parse_config_files(switch_id):
    global interfaces_vlan_t, priority, state
    f = f"configs/switch{switch_id}.cfg"
    # fac match pe fisierele de configurare
    if not os.path.exists(f):
        print("File not found")
        return None
    file = open(f, "r")
    # deschid fisierul de configurare corespunzatorul switch-ului
    if not file:
        print("File not found")
        return None
    
    lines = file.readlines()
    # citesc toate liniile
    priority = int(lines[0].strip())
    # pe prima linie va fi intotdeauna prioritatea
    for i in range(1, len(lines)):
        interface, vlan_type = lines[i].strip().split()
        # pe celelalte linii sunt interfetele, separate prin spatiu de tipul lor
        if vlan_type != 'T':
            vlan_type = int(vlan_type)
            # daca nu este trunk, inseamna ca avem un numar de VLAN si il convertim la int
        if vlan_type == 'T':
            state[interface] = 0
            # setam interfata ca fiind de tip trunk si starea ei ca fiind blocked
        interfaces_vlan_t[interface] = vlan_type

def main():
    global CAM_table, interfaces_vlan_t, priority, state, interfaces_aux
    global root_port, own_bridge_id, root_bridge_id, root_path_cost
    
    # init returns the max interface number. Our interfaces
    # are 0, 1, 2, ..., init_ret value + 1
    switch_id = sys.argv[1]
    
    num_interfaces = wrapper.init(sys.argv[2:])
    interfaces = range(0, num_interfaces)
    interfaces_aux = interfaces

    print("# Starting switch with id {}".format(switch_id), flush=True)
    print("[INFO] Switch MAC", ':'.join(f'{b:02x}' for b in get_switch_mac()))

    # Create and start a new thread that deals with sending BPDU
    t = threading.Thread(target=send_bpdu_every_sec)
    t.start()

    # Printing interface names
    for i in interfaces:
        print(get_interface_name(i))

    parse_config_files(switch_id)
    # parsez datele din fisierele de configurare
    own_bridge_id = priority
    root_bridge_id = own_bridge_id
    root_path_cost = 0
    
    while True:
        # Note that data is of type bytes([...]).
        # b1 = bytes([72, 101, 108, 108, 111])  # "Hello"
        # b2 = bytes([32, 87, 111, 114, 108, 100])  # " World"
        # b3 = b1[0:2] + b[3:4].
        interface, data, length = recv_from_any_link()

        dest_mac, src_mac, ethertype, vlan_id = parse_ethernet_header(data)

        # Print the MAC src and MAC dst in human readable format
        dest_mac = ':'.join(f'{b:02x}' for b in dest_mac)
        src_mac = ':'.join(f'{b:02x}' for b in src_mac)

        # Note. Adding a VLAN tag can be as easy as
        # tagged_frame = data[0:12] + create_vlan_tag(10) + data[12:]

        print(f'Destination MAC: {dest_mac}')
        print(f'Source MAC: {src_mac}')
        print(f'EtherType: {ethertype}')
        print("Received frame of size {} on interface {}".format(length, interface), flush=True)

        # TODO: Implement forwarding with learning
        # TODO: Implement VLAN support
        # TODO: Implement STP support
        CAM_table[src_mac] = interface
        frame_logic(interface, data, length, dest_mac, vlan_id)
        # apelez functia care imi va face forwardarea frame-ului in functie de destinatie

        # data is of type bytes.
        # send_to_link(i, data, length) 

if __name__ == "__main__":
    main()