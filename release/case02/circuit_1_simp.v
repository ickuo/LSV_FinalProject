// Benchmark "top" written by ABC on Sun Dec 21 18:03:23 2025

module top ( 
    n1, n4, n5, n11, n19, n24, n35, n39, n45, n46, n48, n49,
    n6, n16, n36, n44  );
  input  n1, n4, n5, n11, n19, n24, n35, n39, n45, n46, n48, n49;
  output n6, n16, n36, n44;
  wire new_n17, new_n18, new_n19_1, new_n20, new_n21, new_n22, new_n23,
    new_n24_1, new_n25, new_n26, new_n27, new_n28, new_n29, new_n30,
    new_n31, new_n32, new_n33, new_n34, new_n35_1, new_n36_1, new_n37,
    new_n38, new_n39_1, new_n40, new_n41, new_n42, new_n43, new_n45_1,
    new_n46_1, new_n47, new_n48_1, new_n49_1, new_n50, new_n51, new_n52,
    new_n53, new_n54, new_n55, new_n56, new_n57, new_n58, new_n59, new_n61,
    new_n63, new_n64, new_n65;
  assign new_n17 = n1 & n45;
  assign new_n18 = ~n1 & ~n45;
  assign new_n19_1 = ~new_n17 & ~new_n18;
  assign new_n20 = ~n35 & ~n48;
  assign new_n21 = n35 & n48;
  assign new_n22 = n24 & n39;
  assign new_n23 = ~new_n21 & ~new_n22;
  assign new_n24_1 = ~new_n20 & ~new_n23;
  assign new_n25 = ~new_n19_1 & new_n24_1;
  assign new_n26 = new_n19_1 & ~new_n24_1;
  assign new_n27 = ~new_n25 & ~new_n26;
  assign new_n28 = n19 & ~new_n27;
  assign new_n29 = ~n19 & new_n27;
  assign new_n30 = ~new_n28 & ~new_n29;
  assign new_n31 = ~n24 & ~n39;
  assign new_n32 = ~new_n22 & ~new_n31;
  assign new_n33 = n46 & new_n32;
  assign new_n34 = ~n11 & ~new_n33;
  assign new_n35_1 = n11 & new_n33;
  assign new_n36_1 = ~new_n20 & ~new_n21;
  assign new_n37 = ~new_n22 & new_n36_1;
  assign new_n38 = new_n22 & ~new_n36_1;
  assign new_n39_1 = ~new_n37 & ~new_n38;
  assign new_n40 = ~new_n35_1 & new_n39_1;
  assign new_n41 = ~new_n34 & ~new_n40;
  assign new_n42 = ~new_n30 & new_n41;
  assign new_n43 = new_n30 & ~new_n41;
  assign n6 = new_n42 | new_n43;
  assign new_n45_1 = ~new_n17 & ~new_n24_1;
  assign new_n46_1 = ~new_n18 & ~new_n45_1;
  assign new_n47 = ~n5 & n49;
  assign new_n48_1 = n5 & ~n49;
  assign new_n49_1 = ~new_n47 & ~new_n48_1;
  assign new_n50 = n4 & ~new_n49_1;
  assign new_n51 = ~n4 & new_n49_1;
  assign new_n52 = ~new_n50 & ~new_n51;
  assign new_n53 = new_n46_1 & ~new_n52;
  assign new_n54 = ~new_n46_1 & new_n52;
  assign new_n55 = ~new_n53 & ~new_n54;
  assign new_n56 = ~new_n28 & ~new_n41;
  assign new_n57 = ~new_n29 & ~new_n56;
  assign new_n58 = new_n55 & new_n57;
  assign new_n59 = ~new_n55 & ~new_n57;
  assign n16 = new_n58 | new_n59;
  assign new_n61 = ~n46 & ~new_n32;
  assign n36 = ~new_n33 & ~new_n61;
  assign new_n63 = ~new_n34 & ~new_n35_1;
  assign new_n64 = ~new_n39_1 & ~new_n63;
  assign new_n65 = new_n39_1 & new_n63;
  assign n44 = new_n64 | new_n65;
endmodule


