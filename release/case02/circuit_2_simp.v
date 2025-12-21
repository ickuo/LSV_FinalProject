// Benchmark "top" written by ABC on Sun Dec 21 18:05:19 2025

module top ( 
    n3, n4, n9, n13, n15, n30, n31, n38, n40, n44, n48, n49,
    n6, n10, n18, n33  );
  input  n3, n4, n9, n13, n15, n30, n31, n38, n40, n44, n48, n49;
  output n6, n10, n18, n33;
  wire new_n17, new_n18_1, new_n19, new_n20, new_n21, new_n22, new_n23,
    new_n24, new_n25, new_n26, new_n27, new_n28, new_n29, new_n30_1,
    new_n31_1, new_n32, new_n33_1, new_n34, new_n35, new_n36, new_n37,
    new_n38_1, new_n39, new_n40_1, new_n41, new_n42, new_n43, new_n45,
    new_n47, new_n48_1, new_n49_1, new_n51, new_n52, new_n53, new_n54,
    new_n55, new_n56, new_n57, new_n58, new_n59, new_n60, new_n61, new_n62,
    new_n63, new_n64, new_n65;
  assign new_n17 = n3 & n4;
  assign new_n18_1 = ~n3 & ~n4;
  assign new_n19 = ~new_n17 & ~new_n18_1;
  assign new_n20 = n31 & new_n19;
  assign new_n21 = ~n44 & ~new_n20;
  assign new_n22 = n44 & new_n20;
  assign new_n23 = n48 & n49;
  assign new_n24 = ~n48 & ~n49;
  assign new_n25 = ~new_n23 & ~new_n24;
  assign new_n26 = ~new_n17 & ~new_n25;
  assign new_n27 = new_n17 & new_n25;
  assign new_n28 = ~new_n26 & ~new_n27;
  assign new_n29 = ~new_n22 & ~new_n28;
  assign new_n30_1 = ~new_n21 & ~new_n29;
  assign new_n31_1 = n30 & new_n30_1;
  assign new_n32 = ~n30 & ~new_n30_1;
  assign new_n33_1 = ~new_n31_1 & ~new_n32;
  assign new_n34 = n15 & n38;
  assign new_n35 = ~n15 & ~n38;
  assign new_n36 = ~new_n34 & ~new_n35;
  assign new_n37 = ~new_n17 & ~new_n23;
  assign new_n38_1 = ~new_n24 & ~new_n37;
  assign new_n39 = ~new_n36 & new_n38_1;
  assign new_n40_1 = new_n36 & ~new_n38_1;
  assign new_n41 = ~new_n39 & ~new_n40_1;
  assign new_n42 = new_n33_1 & new_n41;
  assign new_n43 = ~new_n33_1 & ~new_n41;
  assign n6 = new_n42 | new_n43;
  assign new_n45 = ~n31 & ~new_n19;
  assign n10 = ~new_n20 & ~new_n45;
  assign new_n47 = ~new_n21 & ~new_n22;
  assign new_n48_1 = new_n28 & ~new_n47;
  assign new_n49_1 = ~new_n28 & new_n47;
  assign n18 = new_n48_1 | new_n49_1;
  assign new_n51 = ~new_n32 & ~new_n41;
  assign new_n52 = ~new_n31_1 & ~new_n51;
  assign new_n53 = ~new_n34 & ~new_n38_1;
  assign new_n54 = ~new_n35 & ~new_n53;
  assign new_n55 = ~n13 & n40;
  assign new_n56 = n13 & ~n40;
  assign new_n57 = ~new_n55 & ~new_n56;
  assign new_n58 = n9 & ~new_n57;
  assign new_n59 = ~n9 & new_n57;
  assign new_n60 = ~new_n58 & ~new_n59;
  assign new_n61 = new_n54 & ~new_n60;
  assign new_n62 = ~new_n54 & new_n60;
  assign new_n63 = ~new_n61 & ~new_n62;
  assign new_n64 = new_n52 & new_n63;
  assign new_n65 = ~new_n52 & ~new_n63;
  assign n33 = ~new_n64 & ~new_n65;
endmodule


