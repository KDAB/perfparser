import qbs
import qbs.FileInfo
import qbs.TextFile

ElfUtilsStaticLib {
    property string arch
    destinationDirectory: project.gendisOutDir
    name: arch + " disasm"
    Depends { name: arch + " disasm header" }
    Depends { name: arch + " mnemonics" }

    // FIXME: QBS-1113
    Rule {
        multiplex: true
        inputsFromDependencies: ["hpp", "mnemonics"]
        Artifact {
            filePath: "dummy.h"
            fileTags: ["hpp"]
        }
        prepare: {
            var cmd = new JavaScriptCommand();
            cmd.silent = true;
            cmd.sourceCode = function() {
                var f = new TextFile(output.filePath, TextFile.WriteOnly);
                f.close();
            }
            return [cmd];
        }
    }

    cpp.includePaths: base.concat([FileInfo.joinPaths(product.sourceDirectory, "..")])
    files: [FileInfo.joinPaths(sourceDirectory, "..",  arch + "_disasm.c")]
}
