#!/usr/bin/env python3
"""Every `native` method of NativeBridge.java has a JNI function in
app_native.cpp with the same return and parameter types, and the other way
round. A mismatch compiles on both sides and fails only on the device.
It also checks that every JNI function that turns a handle into a session
takes the session lock first: the render thread draws under that lock.

    python3 tools/check_jni_signatures.py \
        app/src/main/java/com/dmcrengine/nativeviewer/NativeBridge.java \
        app/src/main/cpp/app_native.cpp
"""
import re, sys
java = open(sys.argv[1], encoding='utf-8').read()
cpp = open(sys.argv[2], encoding='utf-8').read()
java = re.sub(r'/\*.*?\*/', '', java, flags=re.S)
java = re.sub(r'//[^\n]*', '', java)
jmap = {'int':'jint','long':'jlong','float':'jfloat','double':'jdouble','boolean':'jboolean','byte':'jbyte','short':'jshort','char':'jchar','void':'void',
        'String':'jstring','int[]':'jintArray','float[]':'jfloatArray','long[]':'jlongArray','byte[]':'jbyteArray','short[]':'jshortArray','boolean[]':'jbooleanArray','double[]':'jdoubleArray'}
def jt(t):
    t = t.strip()
    if t in jmap: return jmap[t]
    if t.endswith('[]'): return 'jobjectArray'
    return 'jobject'
decl = {}
for m in re.finditer(r'native\s+([\w.\[\]<>]+)\s+(\w+)\s*\(([^)]*)\)\s*;', java, flags=re.S):
    ret, name, params = m.groups()
    ps = [p.strip() for p in params.split(',') if p.strip()]
    types = [jt(re.sub(r'\s+\w+$', '', p).replace('final ', '')) for p in ps]
    decl[name] = (jt(ret), types)
defs = {}
for m in re.finditer(r'JNIEXPORT\s+(\w+)\s+JNICALL\s+Java_com_dmcrengine_nativeviewer_NativeBridge_(\w+)\s*\(([^)]*)\)', cpp, flags=re.S):
    ret, name, params = m.groups()
    ps = [p.strip() for p in params.split(',') if p.strip()]
    types = [re.sub(r'\s+\w+$', '', p).strip() if ' ' in p else p for p in ps]
    defs[name] = (ret, types[2:])
bad = 0
for name, (ret, types) in sorted(decl.items()):
    if name not in defs:
        print('MISSING in C++:', name); bad += 1; continue
    cret, ctypes = defs[name]
    if cret != ret or ctypes != types:
        print('MISMATCH', name, '\n   java:', ret, types, '\n   c++ :', cret, ctypes); bad += 1
for name in sorted(set(defs) - set(decl)):
    print('C++ only (no Java declaration):', name); bad += 1
# A handle is used only under the session lock (from_handle's rule).
for m in re.finditer(r'NativeBridge_(\w+)\s*\([^)]*\)\s*\{', cpp):
    depth, i = 1, m.end()
    while depth and i < len(cpp):
        depth += {'{': 1, '}': -1}.get(cpp[i], 0); i += 1
    body = cpp[m.end():i]
    if 'from_handle(' in body and 'SessionLock' not in body.split('from_handle(')[0]:
        print('NO SESSION LOCK before from_handle:', m.group(1)); bad += 1
print(len(decl), 'Java natives,', len(defs), 'C++ definitions,', bad, 'problems')
sys.exit(1 if bad else 0)
