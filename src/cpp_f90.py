import sys

in_type_map = {
    'void*'  : 'type(C_PTR)',
    'int'    : 'integer(C_INT)',
    'double' : 'real(C_DOUBLE)',
    'string' : 'character(C_CHAR)'
}

def write_str_to_f90(o, string):
    n = 80
    while len(string) > n:
        o.write(string[:n] + '&\n&')
        string = string[n:]
    o.write(string)
    o.write('\n')

def main():
    f = open(sys.argv[1], 'r') 
    o = open('generated.f90', 'w')
    o.write('! Warning! This file is autogenerated using cpp_f90.py script!\n')
    o.write('!\n')
    while (True):
        line = f.readline()
        if not line: break

        if "@fortran begin comment" in line:
            while(True):
                line1 = f.readline()
                if "@fortran end" in line1: break
                o.write(line1)

        
        if "@fortran begin" in line:
            func_args = []
            v = line.split()
            if v[2] == 'function':
                func_type = v[3]
                func_name = v[4]
        if "@fortran argument" in line:
            v = line.split()
            arg_type = v[2]
            arg_intent = v[3]
            if v[4] == 'required':
                arg_required = True
            else:
                arg_required = False
            arg_name = v[5]
            func_args.append({'type' : v[2], 'intent' : v[3], 'required': arg_required, 'name' : v[5]})

        if "@fortran end" in line:
            
            if func_type == 'void':
                string = 'subroutine '
            else:
                string = 'function '
            string = string + func_name + '('
            va = [a['name'] for a in func_args]
            string = string + ','.join(va)
            string = string + ')'
            write_str_to_f90(o, string)

            for a in func_args:
                o.write(in_type_map[a['type']])
                if not a['required']:
                    o.write(', optional, target')
                if a['type'] == 'string':
                    o.write(', dimension(*)')
                o.write(', intent(' + a['intent'] + ') :: ' + a['name'])
                o.write('\n')

            for a in func_args:
                if not a['required']:
                    o.write('type(C_PTR) :: ' + a['name'] + '_ptr = C_NULL_PTR\n')
            o.write('interface\n')

            if func_type == 'void':
                string = 'subroutine '
            else:
                string = 'function '
            string = string + func_name + '_aux('
            va = [a['name'] for a in func_args]
            string = string + ','.join(va)
            string = string + (')&')
            write_str_to_f90(o, string)
            o.write('&bind(C, name="'+func_name+'")\n')

            o.write('use, intrinsic :: ISO_C_BINDING\n')
            for a in func_args:
                if not a['required']:
                    o.write('type(C_PTR)')
                    o.write(', value')
                else:
                    o.write(in_type_map[a['type']])
                    if a['type'] == 'string':
                        o.write(', dimension(*)')
                o.write(', intent(' + a['intent'] + ') :: ' + a['name'])
                o.write('\n')

            if (func_type == 'void'):
                o.write('end subroutine\n')
            o.write('end interface\n')

            for a in func_args:
                if not a['required']:
                    o.write('if (present('+a['name']+')) ' + a['name'] + '_ptr = C_LOC(' + a['name'] + ')\n')
             
            if (func_type == 'void'):
                string = 'call '
            else:
                string = 'result = '
            string = string + func_name + '_aux('
            va = []
            for a in func_args:
                if not a['required']:
                    va.append(a['name'] + '_ptr')
                else:
                    va.append(a['name'])
            string = string + ','.join(va)
            string = string + ')'
            write_str_to_f90(o, string)

            if (func_type == 'void'):
                o.write('end subroutine ')
            o.write(func_name + '\n\n')



    f.close()
    o.close()
if __name__ == "__main__":
    main()
