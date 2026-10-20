puts "BUS1 ports [lsort [lmap o [get_ports *] {get_full_name $o}]]"
puts "BUS1 hier [lsort [lmap o [get_pins h/*] {get_full_name $o}]]"
puts "BUS1 leaf [lsort [lmap o [get_pins h/u/*] {get_full_name $o}]]"
