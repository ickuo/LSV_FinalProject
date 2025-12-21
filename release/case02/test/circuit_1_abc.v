module top( n1 , n4 , n5 , n6 , n11 , n16 , n19 , n24 , n35 , 
n36 , n39 , n44 , n45 , n46 , n48 , n49 );
    input n1 , n4 , n5 , n11 , n19 , n24 , n35 , n39 , n45 , 
n46 , n48 , n49 ;
    output n6 , n16 , n36 , n44 ;
    wire n0 , n2 , n3 , n7 , n8 , n9 , n10 , n12 , n13 , 
n14 , n15 , n17 , n18 , n20 , n21 , n22 , n23 , n25 , n26 , 
n27 , n28 , n29 , n30 , n31 , n32 , n33 , n34 , n37 , n38 , 
n40 , n41 , n42 , n43 , n47 ;
    not ( n38 , n11 );
    xnor ( n43 , n47 , n49 );
    not ( n10 , n30 );
    xnor ( n6 , n7 , n23 );
    or ( n34 , n26 , n8 );
    nor ( n15 , n20 , n7 );
    xnor ( n27 , n35 , n48 );
    xnor ( n41 , n5 , n4 );
    xnor ( n14 , n18 , n3 );
    xnor ( n23 , n14 , n19 );
    xnor ( n31 , n24 , n39 );
    nand ( n13 , n35 , n48 );
    xnor ( n25 , n0 , n11 );
    nor ( n28 , n22 , n18 );
    xnor ( n36 , n31 , n46 );
    and ( n18 , n32 , n13 );
    not ( n8 , n39 );
    and ( n30 , n21 , n46 );
    nor ( n22 , n45 , n1 );
    xnor ( n0 , n34 , n27 );
    nor ( n9 , n35 , n48 );
    xnor ( n3 , n45 , n1 );
    or ( n12 , n38 , n10 );
    and ( n7 , n2 , n12 );
    and ( n20 , n14 , n29 );
    not ( n21 , n31 );
    or ( n17 , n40 , n15 );
    or ( n2 , n0 , n37 );
    or ( n32 , n9 , n34 );
    xnor ( n42 , n41 , n17 );
    xnor ( n16 , n43 , n42 );
    and ( n33 , n1 , n45 );
    nor ( n37 , n11 , n30 );
    xnor ( n44 , n10 , n25 );
    not ( n26 , n24 );
    not ( n29 , n19 );
    nor ( n40 , n29 , n14 );
    or ( n47 , n33 , n28 );
endmodule
