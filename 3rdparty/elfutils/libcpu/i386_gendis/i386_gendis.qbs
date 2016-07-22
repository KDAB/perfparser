import qbs

Project {
    MnemonicsFile { name: "i386 mnemonics"; outputFileName: "i386.mnemonics" }
    MnemonicsFile { name: "x86_64 mnemonics"; outputFileName: "x86_64.mnemonics" }

    ElfUtilsProduct {
        name: "i386_gendis"
        destinationDirectory: project.gendisOutDir
        type: ["application"]
        Depends { name: "eu" }
        Depends { name: "i386 mnemonics" }
        cpp.cFlags: base.concat(["-Wno-unused-function"])
        cpp.dynamicLibraries: base.concat(["m"])
        Group {
            fileTags: ["yacc"]
            files: ["../i386_parse.y"]
        }
        Group {
            fileTags: ["lex"]
            files: ["../i386_lex.l"]
        }
        files: ["../i386_gendis.c"]

        Rule {
            inputs: ["lex"]
            Artifact {
                filePath: "i386_lex.c"
                fileTags: ["c"]
            }
            prepare: {
                var cmd = new Command("flex", ["-Pi386_", "-o", output.filePath, input.filePath]);
                cmd.description = "Creating " + output.fileName;
                return [cmd];
            }
        }

        Rule {
            inputs: ["yacc"]
            Artifact {
                filePath: "i386_parse.c"
                fileTags: ["c"]
            }
            prepare: {
                var cmd = new Command("bison", ["-pi386_", "-d", "-o", output.filePath,
                                      input.filePath]);
                cmd.description = "Creating " + output.fileName;
                return [cmd];
            }
        }
    }
}
