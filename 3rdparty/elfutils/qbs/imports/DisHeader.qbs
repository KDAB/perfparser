import qbs
import qbs.FileInfo

Product {
    type: ["hpp"]
    destinationDirectory: project.gendisOutDir
    property string arch
    name: arch + " disasm header"
    Depends { name: "i386_gendis" }
    Depends { name: arch + " mnemonics" }

    Rule {
        inputsFromDependencies: ["application"]
        Artifact {
            filePath: FileInfo.joinPaths(product.destinationDirectory, product.arch + "_dis.h")
            fileTags: product.type
        }
        prepare: {
            var args = [
                "-f", FileInfo.joinPaths(product.sourceDirectory, "..", "extras.mk"),
                "gendis=" + FileInfo.path(input.filePath),
                "srcdir=" + FileInfo.joinPaths(product.sourceDirectory, ".."),
                output.fileName
            ];
            var cmd = new Command("make", args);
            cmd.description = "Creating " + output.fileName;
            cmd.workingDirectory = product.destinationDirectory;
            return [cmd];
        }
    }
    Export {
        Depends { name: "cpp" }
        cpp.includePaths: [product.destinationDirectory]
    }
}
