#if !defined(INSPECT_H)
#define INSPECT_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <unordered_map>

#include <elf/elf++.hh>
#include <dwarf/dwarf++.hh>

static const char* find_subprogram(intptr_t search_addr, const dwarf::die& node) {
  if(node.tag == dwarf::DW_TAG::subprogram &&
      node.has(dwarf::DW_AT::low_pc) &&
      node.has(dwarf::DW_AT::high_pc) &&
      node.has(dwarf::DW_AT::name)) {
    
    intptr_t low_pc = node[dwarf::DW_AT::low_pc].as_address();
    intptr_t high_pc = low_pc + node[dwarf::DW_AT::high_pc].as_sconstant();
    if(low_pc <= search_addr && high_pc > search_addr) {
      return node[dwarf::DW_AT::name].as_cstr();
    }
  }

  for(auto& child: node) {
    const char* result = find_subprogram(search_addr, child);
    if(result != NULL) return result;
  }

  return NULL;
}

static const char* address_to_function(pid_t pid, void* addr) {
  // Map of dwarf information by filename
  static std::map<std::string, dwarf::dwarf> dwarf_map;

  // Open the /proc/<pid>/maps file for processing
  char maps_filename[32];
  snprintf(maps_filename, 32, "/proc/%d/maps", pid);
  
  FILE* maps_file = fopen(maps_filename, "r");
  if(maps_file == NULL) return NULL; 

  // Read the maps file until we find a matching entry or reach the end
  intptr_t search_address = (intptr_t)addr;
  intptr_t start_addr;
  intptr_t end_addr;
  char permissions[5];
  size_t offset;
  char device[32];
  long long inode;
  char mapped_file[256];

  char* line = NULL;
  size_t len = 0;

  bool match_found = false;

  while(!match_found && getline(&line, &len, maps_file) != EOF) {
    sscanf(line, "%lx-%lx %s %lx %s %lld %s", 
                 &start_addr, &end_addr, permissions, &offset, device, &inode, mapped_file);

    if(search_address >= start_addr && search_address < end_addr) {
      match_found = true;
    }
  }

  free(line);
  fclose(maps_file);

  // Return failure if there was no matching entry in /proc/<pid>/maps
  if(!match_found) return NULL;

  auto d = dwarf_map.find(mapped_file);

  if(d == dwarf_map.end()) {
    int fd = open(mapped_file, O_RDONLY);
    if(fd == -1) return NULL;

    elf::elf f(elf::create_mmap_loader(fd));
    if(!f.valid()) {
      close(fd);
      return NULL;
    }

    // If this is a dynamically relocated executable, adjust our search address to the offset within the mapped section
    if(f.get_hdr().type == elf::et::dyn) {
      search_address -= start_addr;
    }

    dwarf_map.emplace(mapped_file, dwarf::elf::create_loader(f));
    d = dwarf_map.find(mapped_file);

    // Just to be sure, make sure we actually inserted
    if(d == dwarf_map.end()) return NULL;
  }

  for(auto cu: d->second.compilation_units()) {
    const char* result = find_subprogram(search_address, cu.root());
    if(result != NULL) return result;
  }

  return NULL;
}

#endif

