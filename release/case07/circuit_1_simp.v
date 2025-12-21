// Benchmark "top" written by ABC on Sun Dec 21 18:09:33 2025

module top ( 
    n0, n3, n11, n15, n21, n35, n36, n37, n41, n61, n73, n80,
    n4, n10, n33, n39, n66, n70, n71, n79  );
  input  n0, n3, n11, n15, n21, n35, n36, n37, n41, n61, n73, n80;
  output n4, n10, n33, n39, n66, n70, n71, n79;
  wire new_n21_1, new_n22, new_n23, new_n24, new_n25, new_n26, new_n27,
    new_n28, new_n29, new_n30, new_n31, new_n32, new_n33_1, new_n34,
    new_n35_1, new_n36_1, new_n37_1, new_n38, new_n39_1, new_n40,
    new_n41_1, new_n42, new_n43, new_n44, new_n45, new_n46, new_n47,
    new_n48, new_n49, new_n50, new_n51, new_n52, new_n53, new_n54, new_n55,
    new_n57, new_n58, new_n59, new_n60, new_n61_1, new_n62, new_n63,
    new_n64, new_n65, new_n67, new_n70_1, new_n71_1, new_n72, new_n74,
    new_n75, new_n76, new_n79_1, new_n80_1;
  assign new_n21_1 = ~n3 & ~n21;
  assign new_n22 = n3 & ~n73;
  assign new_n23 = n37 & ~new_n22;
  assign new_n24 = ~new_n21_1 & new_n23;
  assign new_n25 = n3 & n80;
  assign new_n26 = ~n3 & n41;
  assign new_n27 = ~n37 & ~new_n26;
  assign new_n28 = ~new_n25 & new_n27;
  assign new_n29 = ~new_n24 & ~new_n28;
  assign new_n30 = ~n21 & ~n35;
  assign new_n31 = n35 & ~n73;
  assign new_n32 = n11 & ~new_n31;
  assign new_n33_1 = ~new_n30 & new_n32;
  assign new_n34 = ~n0 & ~n21;
  assign new_n35_1 = n0 & ~n73;
  assign new_n36_1 = n15 & ~new_n35_1;
  assign new_n37_1 = ~new_n34 & new_n36_1;
  assign new_n38 = ~new_n33_1 & ~new_n37_1;
  assign new_n39_1 = n0 & n80;
  assign new_n40 = ~n0 & n41;
  assign new_n41_1 = ~n15 & ~new_n40;
  assign new_n42 = ~new_n39_1 & new_n41_1;
  assign new_n43 = ~new_n38 & ~new_n42;
  assign new_n44 = ~n21 & ~n61;
  assign new_n45 = n61 & ~n73;
  assign new_n46 = n36 & ~new_n45;
  assign new_n47 = ~new_n44 & new_n46;
  assign new_n48 = ~new_n43 & ~new_n47;
  assign new_n49 = n61 & n80;
  assign new_n50 = n41 & ~n61;
  assign new_n51 = ~n36 & ~new_n50;
  assign new_n52 = ~new_n49 & new_n51;
  assign new_n53 = ~new_n48 & ~new_n52;
  assign new_n54 = ~new_n29 & new_n53;
  assign new_n55 = new_n29 & ~new_n53;
  assign n4 = new_n54 | new_n55;
  assign new_n57 = n35 & n80;
  assign new_n58 = ~n35 & n41;
  assign new_n59 = ~n11 & ~new_n58;
  assign new_n60 = ~new_n57 & new_n59;
  assign new_n61_1 = ~new_n37_1 & new_n60;
  assign new_n62 = ~new_n42 & ~new_n61_1;
  assign new_n63 = ~new_n47 & ~new_n62;
  assign new_n64 = ~new_n52 & ~new_n63;
  assign new_n65 = ~new_n24 & ~new_n64;
  assign n33 = ~new_n28 & ~new_n65;
  assign new_n67 = new_n38 & ~new_n47;
  assign n71 = new_n24 | ~new_n67;
  assign n10 = ~n33 | ~n71;
  assign new_n70_1 = ~new_n47 & ~new_n52;
  assign new_n71_1 = ~new_n43 & new_n70_1;
  assign new_n72 = new_n43 & ~new_n70_1;
  assign n39 = new_n71_1 | new_n72;
  assign new_n74 = ~new_n37_1 & ~new_n42;
  assign new_n75 = new_n33_1 & new_n74;
  assign new_n76 = ~new_n33_1 & ~new_n74;
  assign n66 = ~new_n75 & ~new_n76;
  assign n70 = ~new_n33_1 & ~new_n60;
  assign new_n79_1 = n39 & new_n74;
  assign new_n80_1 = n70 & new_n79_1;
  assign n79 = n4 & new_n80_1;
endmodule


