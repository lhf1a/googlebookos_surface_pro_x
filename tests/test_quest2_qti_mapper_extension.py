"""Compile the real extension against pinned ABI headers and a stateful mapper double."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(add_help=False)
parser.add_argument("--mapper-include",type=Path,default=ROOT/"out/qualcomm-full-stack-20261108/mapper5/include")
parser.add_argument("--aidl-headers",type=Path,default=ROOT/"out/quest2-video-618-20261107/all-codecs/renderer-p010-source/generated")
parser.add_argument("--binder-source",type=Path,default=Path("/mnt/spx-workspace/toolchains/sq2-c2-native-reference/libs/binder/ndk"))
ARGS,REST=parser.parse_known_args()
class Extension(unittest.TestCase):
    def test_real_source_ownership_errors_and_explicit_metadata_state(self):
        refs=json.loads((ROOT/"configs/quest2-qti-mapper-extension-reference.json").read_text())
        for entry in refs["sources"]:
            self.assertEqual(hashlib.sha256((ROOT/entry["path"]).read_bytes()).hexdigest(),entry["sha256"])
        header=ARGS.mapper_include/"android/hardware/graphics/mapper/IMapper.h"
        pin=next(x for x in json.loads((ROOT/"configs/quest2-mapper5-reference.json").read_text())["sources"] if x["path"].endswith("/IMapper.h"))
        self.assertEqual(hashlib.sha256(header.read_bytes()).hexdigest(),pin["sha256"])
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)
            for name in ["quest2-qti-mapper-extension.cpp"]:
                shutil.copyfile(ROOT/"probe/graphics"/name,p/name)
            shutil.copytree(ROOT/"probe/graphics/qti-mapper-abi",p/"qti-mapper-abi")
            for name,text in {
                "quest2-native-mapper-loader.h":"#pragma once\ninline AIMapper_Error quest2LoadNativeMapper(AIMapper **out){return AIMapper_loadIMapper(out); }\n",
                "cutils/native_handle.h":"#pragma once\nstruct native_handle {int version,numFds,numInts;int data[0];};\ntypedef struct native_handle native_handle_t;typedef const native_handle_t *buffer_handle_t;\n",
                "android/rect.h":"#pragma once\ntypedef struct ARect {int left,top,right,bottom;} ARect;\n",
                "quest2-gralloc-metadata.h":"#pragma once\nstruct MetaData_t {bool isStandardMetadataSet[512]{},isVendorMetadataSet[512]{};};\nMetaData_t *fixtureMetadata(buffer_handle_t);\nclass QuestMetadata {buffer_handle_t b;public:explicit QuestMetadata(buffer_handle_t h):b(h){} MetaData_t *get(){return fixtureMetadata(b);}};\n"+
                    "\n".join("#define QTI_"+n+" "+str(v) for n,v in [("VT_TIMESTAMP",10000),("PP_PARAM_INTERLACED",10002),("UBWC_CR_STATS_INFO",10005),("CVP_METADATA",10010),("VIDEO_TRANSCODE_STATS",10030),("STANDARD_METADATA_STATUS",10016),("VENDOR_METADATA_STATUS",10017)])+"\n",
            }.items():
                path=p/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)
            target=p/"extension-test"
            command=["g++","-std=c++20","-O0","-g","-Wall","-Wextra","-Werror","-Wno-misleading-indentation",
                     "-D_Nonnull=","-D_Nullable=","-D_Null_unspecified=","-fsanitize=address,undefined","-fno-omit-frame-pointer",
                     "-I",str(p),"-I",str(ARGS.mapper_include),"-isystem",str(ARGS.aidl_headers),
                     "-isystem",str(ARGS.binder_source/"include_cpp"),"-isystem",str(ARGS.binder_source/"include_ndk"),str(ROOT/"tests/quest2_qti_mapper_extension_test.cpp"),"-o",str(target)]
            subprocess.run(command,check=True)
            run=subprocess.run([str(target)],capture_output=True,text=True,env={**os.environ,"UBSAN_OPTIONS":"halt_on_error=1","ASAN_OPTIONS":"detect_leaks=1"})
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn("QTI_EXTENSION_OWNERSHIP_AND_METADATA_STATE_PASS",run.stdout)
if __name__=="__main__":unittest.main(argv=[__file__]+REST)
