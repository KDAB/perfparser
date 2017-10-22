import qbs

Project {
    name: "PerfParserAutotests"
    condition: project.withAutotests
    references: [
        "addresscache", "elfmap", "kallsyms", "perfdata", "perfstdin"
    ]
}
