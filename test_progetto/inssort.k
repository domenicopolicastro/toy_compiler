global VETT[5];
extern printval(v control);

def main() {
  var val_letto = 0;

  VETT[1] = 10.5;      
  VETT[3] = VETT[1] + 2.0; 

  printval(VETT[0], 0); 
  printval(VETT[1], 0); 
  printval(VETT[3], 0)

};