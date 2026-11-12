#pragma once
struct PersonDef { const char* id; const char* name; const char* card; byte group; bool teacher; };
const char* const CLASSES[] = {"Ingeniería", "Economía"};
constexpr PersonDef PEOPLE[] = {
 {"ALU_01","Lucía Fernández","A7F3C901",0,false},
 {"ALU_02","Martina Silva","A7F3C902",0,false},
 {"ALU_03","Mateo Rodríguez","A7F3C903",0,false},
 {"ALU_04","Valentina Pereira","A7F3C904",1,false},
 {"ALU_05","Santiago López","A7F3C905",1,false},
 {"ALU_06","Nicolás González","A7F3C906",1,false},
 {"PROF_01","Ana Martínez","B8D4E101",0,true},
 {"PROF_02","Martín Cabrera","B8D4E102",1,true}
};
constexpr size_t PERSON_COUNT=sizeof(PEOPLE)/sizeof(PEOPLE[0]);
constexpr size_t CLASS_COUNT=sizeof(CLASSES)/sizeof(CLASSES[0]);
constexpr bool validGroups(){
 for(const auto& person:PEOPLE)if(person.group>=CLASS_COUNT)return false;
 return PERSON_COUNT>0&&CLASS_COUNT>0;
}
static_assert(validGroups(),"Cada persona debe referirse a una clase existente");
