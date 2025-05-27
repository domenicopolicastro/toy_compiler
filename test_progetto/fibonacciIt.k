def fibo(n) {
   var a = 0;
   var b = 1;
   var i = 1;
   for (i = 1; i<n; ++i) {
       var oldb = b;
       b = a+b;
       a = oldb
   };
   b 
};
