const char *lily_dynaload_table[] = {
    "\02Result\0Conn\0"
    ,"C\03Result"
    ,"m:close\0(Result)"
    ,"m:each_row\0(Result,Function(List[String]))"
    ,"m:row_count\0(Result):Integer"
    ,"C\02Conn"
    ,"m:query\0(Conn,String,List[String]...):Either[String,Result]"
    ,"m:open\0(*String,*String,*String,*String,*String):Option[Conn]"
    ,"Z"
};
