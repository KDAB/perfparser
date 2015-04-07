import qbs
import qbs.FileInfo
import qbs.TextFile

Product {
    type: ["mnemonics"]
    destinationDirectory: project.gendisOutDir
    property string outputFileName
    property string headerFile: "linecount.h"
    Group {
        name: "Makefile"
        fileTags: ["makefile"]
        files: ["../extras.mk"]
    }
    Group {
        name: "defs file"
        fileTags: ["defs"]
        files: ["../defs/i386"]
    }

    Rule {
        inputs: ["makefile", "defs"]
        multiplex: true
        Artifact {
            filePath: FileInfo.joinPaths(product.destinationDirectory, product.outputFileName)
            fileTags: product.type
        }
        Artifact {
            filePath: product.headerFile
            fileTags: ["hpp"]
        }

        prepare: {
            var makeArgs = ["-f", inputs["makefile"][0].filePath,
                            "srcdir=" + product.sourceDirectory + "/..",
                            outputs["mnemonics"][0].fileName];
            var makeCmd = new Command("make", makeArgs);
            makeCmd.description = "Creating " + outputs["mnemonics"][0].fileName;
            makeCmd.workingDirectory = product.destinationDirectory;
            var headerCmd = new JavaScriptCommand();
            headerCmd.description = "Creating " + outputs["hpp"][0].fileName;
            headerCmd.sourceCode = function() {
                var iFile = new TextFile(outputs["mnemonics"][0].filePath, TextFile.ReadOnly);
                var count = 0;
                while (!iFile.atEof()) {
                    ++count;
                    iFile.readLine();
                }
                iFile.close();
                var oFile = new TextFile(outputs["hpp"][0].filePath, TextFile.WriteOnly);
                oFile.writeLine("#define NMNES " + count);
                oFile.close();
            }

            return [makeCmd, headerCmd];
        }
    }

    Export {
        Depends { name: "cpp" }
        cpp.cFlags: ["-include", FileInfo.joinPaths(product.buildDirectory, product.headerFile)]
        cpp.includePaths: [product.destinationDirectory]
    }
}
